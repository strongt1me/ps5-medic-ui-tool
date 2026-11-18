// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP-GET ueber BSD-Sockets (Linux/WSL und PS5), https:// mit BearSSL (third_party/bearssl) und eingebauten Wurzelzertifikaten
// (tls_roots.c). Siehe medic/http.hpp fuer die Grenzen.
#include "medic/http.hpp"

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "bearssl/inc/bearssl.h"

extern "C" const br_x509_trust_anchor* medic_tls_anchors(size_t* count);  // tls_roots.c

// Zufall fuer TLS: Linux getentropy, FreeBSD (auch PS5) arc4random_buf, sonst /dev/urandom. false, wenn nichts davon geht
// (dann wird nicht verbunden: lieber kein TLS als schwaches).
static bool fill_random(unsigned char* buf, std::size_t n) {
#if defined(__linux__)
    std::size_t done = 0;
    while (done < n) {
        const std::size_t part = std::min<std::size_t>(n - done, 256);
        if (getentropy(buf + done, part) != 0) break;
        done += part;
    }
    if (done == n) return true;
#elif defined(__FreeBSD__)
    arc4random_buf(buf, n);
    return true;
#endif
    if (std::FILE* f = std::fopen("/dev/urandom", "rb")) {
        const std::size_t got = std::fread(buf, 1, n, f);
        std::fclose(f);
        return got == n;
    }
    return false;
}

// BearSSL ruft diese Funktion, wenn seine Zufallsquelle noch keinen Anfangswert hat (Ersatz fuer rand/sysrng.c, das nicht gebaut wird).
static int medic_seed_prng(const br_prng_class** ctx) {
    unsigned char b[48];
    if (!fill_random(b, sizeof b)) return 0;
    (*ctx)->update(ctx, b, sizeof b);
    return 1;
}

extern "C" br_prng_seeder br_prng_seeder_system(const char** name) {
    if (name) *name = "medic";
    return &medic_seed_prng;
}

namespace medic {

namespace {

using Clock = std::chrono::steady_clock;

struct Fd {
    int fd = -1;
    ~Fd() {
        if (fd >= 0) close(fd);
    }
};

int ms_left(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left < 0 ? 0 : static_cast<int>(left);
}

// Wartet auf `events`; 1 = bereit, 0 = Zeit abgelaufen, -1 = Fehler.
int wait_fd(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        struct pollfd pfd = {fd, events, 0};
        const int n = poll(&pfd, 1, ms_left(deadline));
        if (n > 0) return 1;
        if (n == 0) return 0;
        if (errno != EINTR) return -1;
    }
}

bool resolve_blocking(const HttpUrl& u, struct sockaddr_in& out) {
    std::memset(&out, 0, sizeof out);
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<unsigned short>(u.port));
    if (inet_pton(AF_INET, u.host.c_str(), &out.sin_addr) == 1) return true;
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(u.host.c_str(), nullptr, &hints, &res) != 0 || !res) return false;
    out.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// Namensaufloesung mit dem gemeinsamen Zeitlimit: getaddrinfo selbst kennt keins und blockiert bei einem toten Resolver lange. Es laeuft in einem
// eigenen Thread; laeuft die Zeit ab, wird er losgelassen (er endet allein, der Zustand gehoert ihm gemeinsam). 1 = ok, 0 = nicht aufloesbar, -1 = Zeit.
int resolve(const HttpUrl& u, struct sockaddr_in& out, Clock::time_point deadline) {
    struct in_addr literal;
    if (inet_pton(AF_INET, u.host.c_str(), &literal) == 1) return resolve_blocking(u, out) ? 1 : 0;  // IP-Adresse: kein DNS noetig
    struct Shared {
        std::mutex m;
        std::condition_variable cv;
        bool done = false, ok = false;
        struct sockaddr_in addr;
    };
    auto st = std::make_shared<Shared>();
    std::memset(&st->addr, 0, sizeof st->addr);
    try {
        std::thread([st, u]() {
            struct sockaddr_in a;
            const bool ok = resolve_blocking(u, a);
            std::lock_guard<std::mutex> lk(st->m);
            st->ok = ok;
            if (ok) st->addr = a;
            st->done = true;
            st->cv.notify_all();
        }).detach();
    } catch (...) {
        return resolve_blocking(u, out) ? 1 : 0;  // kein Thread moeglich: wie bisher blockierend
    }
    std::unique_lock<std::mutex> lk(st->m);
    if (!st->cv.wait_until(lk, deadline, [&] { return st->done; })) return -1;
    if (!st->ok) return 0;
    out = st->addr;
    return 1;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Wert eines Headers (Name klein geschrieben, ohne Doppelpunkt), leer wenn er fehlt.
std::string header_value(const std::string& head, const std::string& name) {
    std::size_t pos = head.find("\r\n");  // Statuszeile ueberspringen
    while (pos != std::string::npos) {
        const std::size_t start = pos + 2;
        const std::size_t end = head.find("\r\n", start);
        const std::string line = head.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos && lower(line.substr(0, colon)) == name) {
            std::size_t b = colon + 1;
            while (b < line.size() && (line[b] == ' ' || line[b] == '\t')) ++b;
            std::size_t e = line.size();
            while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t')) --e;
            return line.substr(b, e - b);
        }
        pos = end;
    }
    return {};
}

// Chunked-Koerper entpacken. false bei Formfehlern oder wenn das Ende fehlt.
bool dechunk(const std::string& in, std::size_t max_body, std::string& out, bool& too_large) {
    out.clear();
    too_large = false;
    std::size_t p = 0;
    for (;;) {
        const std::size_t eol = in.find("\r\n", p);
        if (eol == std::string::npos) return false;
        std::string size_text = in.substr(p, eol - p);
        if (const auto semi = size_text.find(';'); semi != std::string::npos) size_text.erase(semi);
        if (size_text.empty() || size_text.size() > 8 || !std::all_of(size_text.begin(), size_text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) return false;
        const std::size_t n = std::strtoul(size_text.c_str(), nullptr, 16);
        p = eol + 2;
        if (n == 0) return true;
        if (out.size() + n > max_body) {
            too_large = true;
            return false;
        }
        if (p + n + 2 > in.size()) return false;
        out.append(in, p, n);
        p += n + 2;  // Daten + CRLF
    }
}

// Die Steckdose fuer BearSSL: nicht blockierend mit poll() und dem gemeinsamen Zeitlimit.
struct SockCtx {
    int fd = -1;
    Clock::time_point deadline;
    bool timeout = false;  // Zeit abgelaufen
    bool failed = false;   // Socket-Fehler
    bool eof = false;      // Gegenstelle hat die Verbindung geschlossen
};

int sock_read(void* c, unsigned char* buf, std::size_t len) {
    auto* s = static_cast<SockCtx*>(c);
    for (;;) {
        const int w = wait_fd(s->fd, POLLIN, s->deadline);
        if (w == 0) {
            s->timeout = true;
            return -1;
        }
        if (w < 0) {
            s->failed = true;
            return -1;
        }
        const ssize_t n = recv(s->fd, buf, len, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            s->failed = true;
            return -1;
        }
        if (n == 0) {
            s->eof = true;
            return -1;  // BearSSL erlaubt keine 0
        }
        return static_cast<int>(n);
    }
}

int sock_write(void* c, const unsigned char* buf, std::size_t len) {
    auto* s = static_cast<SockCtx*>(c);
    for (;;) {
        const int w = wait_fd(s->fd, POLLOUT, s->deadline);
        if (w == 0) {
            s->timeout = true;
            return -1;
        }
        if (w < 0) {
            s->failed = true;
            return -1;
        }
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const ssize_t n = send(s->fd, buf, len, flags);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            s->failed = true;
            return -1;
        }
        return static_cast<int>(n);
    }
}

// Kurzname eines BearSSL-Fehlers fuer die Anzeige ("BearSSL 62 (nicht vertrauenswuerdig)").
std::string tls_error_text(int err) {
    const char* name = nullptr;
    switch (err) {
        case BR_ERR_X509_NOT_TRUSTED: name = "Zertifikat nicht vertrauenswuerdig"; break;
        case BR_ERR_X509_EXPIRED: name = "Zertifikat abgelaufen oder noch nicht gueltig (Uhrzeit der Konsole?)"; break;
        case BR_ERR_X509_BAD_SERVER_NAME: name = "Zertifikat gehoert nicht zum Servernamen"; break;
        case BR_ERR_X509_TIME_UNKNOWN: name = "Uhrzeit unbekannt"; break;
        case BR_ERR_X509_EMPTY_CHAIN: name = "keine Zertifikatskette"; break;
        case BR_ERR_X509_UNSUPPORTED: name = "nicht unterstuetzter Zertifikatstyp"; break;
        case BR_ERR_UNSUPPORTED_VERSION: name = "TLS-Version nicht unterstuetzt"; break;
        case BR_ERR_BAD_PARAM: name = "Parameter"; break;
        case BR_ERR_IO: name = "Verbindung"; break;
        default: break;
    }
    char buf[128];
    if (name) std::snprintf(buf, sizeof buf, "BearSSL %d (%s)", err, name);
    else std::snprintf(buf, sizeof buf, "BearSSL %d", err);
    return buf;
}

// Eine TLS-Sitzung auf einem verbundenen Socket (Heap: der Puffer ist gut 30 KB gross).
struct TlsSession {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    br_sslio_context ioc;
};

class PosixHttpClient : public IHttpClient {
public:
    HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) override {
        HttpResponse r;
        HttpUrl u;
        if (!parse_http_url(url, u, r.error)) return r;
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

        struct sockaddr_in sa;
        if (const int rr = resolve(u, sa, deadline); rr != 1) {
            r.error = rr < 0 ? "timeout" : "resolve";
            return r;
        }
        Fd s;
        s.fd = socket(AF_INET, SOCK_STREAM, 0);
        if (s.fd < 0) {
            r.error = "network";
            return r;
        }
        fcntl(s.fd, F_SETFL, fcntl(s.fd, F_GETFL, 0) | O_NONBLOCK);
        if (connect(s.fd, reinterpret_cast<struct sockaddr*>(&sa), sizeof sa) != 0) {
            if (errno != EINPROGRESS) {
                r.error = "connect";
                return r;
            }
            const int w = wait_fd(s.fd, POLLOUT, deadline);
            if (w == 0) {
                r.error = "timeout";
                return r;
            }
            int err = 0;
            socklen_t len = sizeof err;
            if (w < 0 || getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                r.error = "connect";
                return r;
            }
        }

        // Anfrage: nur GET, Host und eine feste Kennung. Nichts ueber den Nutzer oder die Konsole.
        const bool default_port = u.port == (u.tls ? 443 : 80);
        const std::string host_header = default_port ? u.host : u.host + ":" + std::to_string(u.port);
        const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + host_header + "\r\nUser-Agent: PS5-Medic\r\nAccept: */*\r\nConnection: close\r\n\r\n";

        SockCtx sock;
        sock.fd = s.fd;
        sock.deadline = deadline;
        std::unique_ptr<TlsSession> tls;
        if (u.tls) {
            tls = std::make_unique<TlsSession>();
            size_t n_anchors = 0;
            const br_x509_trust_anchor* anchors = medic_tls_anchors(&n_anchors);
            br_ssl_client_init_full(&tls->sc, &tls->xc, anchors, n_anchors);
            br_ssl_engine_set_versions(&tls->sc.eng, BR_TLS12, BR_TLS12);
            br_ssl_engine_set_buffer(&tls->sc.eng, tls->iobuf, sizeof tls->iobuf, 1);
            unsigned char seed[48];
            if (!fill_random(seed, sizeof seed)) {
                r.error = "tls";
                r.detail = "kein Zufall verfuegbar";
                return r;
            }
            br_ssl_engine_inject_entropy(&tls->sc.eng, seed, sizeof seed);
            // Die Pruefung der Gueltigkeitsdauer nimmt die Uhr der Konsole: Tage seit 0000-01-01 und Sekunden des Tages.
            const std::time_t now = std::time(nullptr);
            br_x509_minimal_set_time(&tls->xc, static_cast<uint32_t>(now / 86400 + 719528), static_cast<uint32_t>(now % 86400));
            br_ssl_client_reset(&tls->sc, u.host.c_str(), 0);
            br_sslio_init(&tls->ioc, &tls->sc.eng, sock_read, &sock, sock_write, &sock);
        }

        // Nach einem Fehler: Zeit, Socket oder TLS?
        const auto fail_io = [&]() {
            if (sock.timeout) r.error = "timeout";
            else if (tls && !sock.failed && !sock.eof) {
                r.error = "tls";
                r.detail = tls_error_text(br_ssl_engine_last_error(&tls->sc.eng));
            } else if (tls && sock.eof) {
                r.error = "tls";
                r.detail = "Verbindung vom Server beendet (Handshake)";
            } else r.error = "network";
        };

        if (tls) {
            if (br_sslio_write_all(&tls->ioc, req.data(), req.size()) < 0 || br_sslio_flush(&tls->ioc) < 0) {
                fail_io();
                return r;
            }
        } else {
            std::size_t sent = 0;
            while (sent < req.size()) {
                const int n = sock_write(&sock, reinterpret_cast<const unsigned char*>(req.data()) + sent, req.size() - sent);
                if (n < 0) {
                    fail_io();
                    return r;
                }
                sent += static_cast<std::size_t>(n);
            }
        }

        // Antwort lesen: bis Verbindungsende oder bis der Koerper laut Content-Length komplett ist.
        const std::size_t raw_cap = max_body + 65536;
        std::string raw;
        std::size_t head_end = std::string::npos;
        long content_length = -1;
        bool chunked = false;
        char buf[8192];
        for (;;) {
            int n;
            if (tls) {
                n = br_sslio_read(&tls->ioc, buf, sizeof buf);
                if (n < 0) {
                    // Sauberes Ende (close_notify) oder Abbruch der Verbindung nach dem Handshake zaehlt als Ende der Daten; ob die Antwort
                    // vollstaendig ist, entscheidet die Laenge weiter unten.
                    const int err = br_ssl_engine_last_error(&tls->sc.eng);
                    if (err == BR_ERR_OK || (err == BR_ERR_IO && sock.eof)) break;
                    fail_io();
                    return r;
                }
            } else {
                n = sock_read(&sock, reinterpret_cast<unsigned char*>(buf), sizeof buf);
                if (n < 0) {
                    if (sock.eof) break;
                    fail_io();
                    return r;
                }
            }
            raw.append(buf, static_cast<std::size_t>(n));
            if (raw.size() > raw_cap) {
                r.error = "too_large";
                return r;
            }
            if (head_end == std::string::npos) {
                head_end = raw.find("\r\n\r\n");
                if (head_end == std::string::npos) {
                    if (raw.size() > 16384) {
                        r.error = "bad_response";
                        return r;
                    }
                    continue;
                }
                const std::string head = raw.substr(0, head_end);
                const std::string cl = header_value(head, "content-length");
                if (!cl.empty() && std::all_of(cl.begin(), cl.end(), [](unsigned char c) { return std::isdigit(c) != 0; }) && cl.size() < 12) content_length = std::atol(cl.c_str());
                chunked = lower(header_value(head, "transfer-encoding")).find("chunked") != std::string::npos;
                if (!chunked && content_length > static_cast<long>(max_body)) {
                    // Eine grosse Fehlerseite (404/403 als HTML) ist ein Status, kein "zu gross": den Status melden, den Koerper verwerfen
                    const int st = head.size() >= 12 ? std::atoi(head.substr(9, 3).c_str()) : 0;
                    if (st >= 400 && head.compare(0, 5, "HTTP/") == 0) r.status = st;
                    else r.error = "too_large";
                    return r;
                }
            }
            if (!chunked && content_length >= 0 && raw.size() >= head_end + 4 + static_cast<std::size_t>(content_length)) break;
            if (chunked) {
                const std::size_t bs = head_end + 4;  // Beginn des Koerpers
                const bool tail = raw.size() >= bs + 5 && raw.compare(raw.size() - 5, 5, "0\r\n\r\n") == 0 && (raw.size() == bs + 5 || raw.compare(raw.size() - 7, 2, "\r\n") == 0);
                if (tail) {  // Schluss-Chunk gesehen: ist der Koerper vollstaendig, muss man nicht auf das Verbindungsende warten
                    std::string tmp;
                    bool tl = false;
                    if (dechunk(raw.substr(bs), max_body, tmp, tl)) break;
                }
            }
        }

        if (head_end == std::string::npos) {
            r.error = "bad_response";
            return r;
        }
        const std::string head = raw.substr(0, head_end);
        // Statuszeile: "HTTP/1.x NNN ..."
        if (head.size() < 12 || head.compare(0, 5, "HTTP/") != 0 || head[8] != ' ' || !std::isdigit(static_cast<unsigned char>(head[9])) ||
            !std::isdigit(static_cast<unsigned char>(head[10])) || !std::isdigit(static_cast<unsigned char>(head[11]))) {
            r.error = "bad_response";
            return r;
        }
        r.status = std::atoi(head.substr(9, 3).c_str());
        if (r.status >= 300 && r.status < 400) {
            r.error = "redirect";
            return r;
        }
        std::string body_raw = raw.substr(head_end + 4);
        if (chunked) {
            bool too_large = false;
            if (!dechunk(body_raw, max_body, r.body, too_large)) {
                r.body.clear();
                r.error = too_large ? "too_large" : "bad_response";
            }
            return r;
        }
        if (content_length >= 0) {
            if (body_raw.size() < static_cast<std::size_t>(content_length)) {  // Verbindung vor dem Ende abgebrochen
                r.error = "bad_response";
                return r;
            }
            body_raw.resize(static_cast<std::size_t>(content_length));
        }
        if (body_raw.size() > max_body) {
            r.error = "too_large";
            return r;
        }
        r.body = std::move(body_raw);
        return r;
    }
};

}  // namespace

std::unique_ptr<IHttpClient> make_posix_http_client() { return std::make_unique<PosixHttpClient>(); }

}  // namespace medic
