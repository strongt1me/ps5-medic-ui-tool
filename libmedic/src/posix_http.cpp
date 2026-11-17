// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP-GET ueber BSD-Sockets (Linux/WSL und PS5). Siehe medic/http.hpp fuer die Grenzen.
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
#include <cstdlib>
#include <cstring>

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

bool resolve(const HttpUrl& u, struct sockaddr_in& out) {
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

class PosixHttpClient : public IHttpClient {
public:
    HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) override {
        HttpResponse r;
        HttpUrl u;
        if (!parse_http_url(url, u, r.error)) return r;
        if (u.tls) {  // TLS gibt es nur ueber das System der Konsole (ps5/ps5_http.cpp), nicht ueber BSD-Sockets
            r.error = "https_unsupported";
            return r;
        }
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);

        struct sockaddr_in sa;
        if (!resolve(u, sa)) {
            r.error = "resolve";
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
        const std::string host_header = u.port == 80 ? u.host : u.host + ":" + std::to_string(u.port);
        const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + host_header + "\r\nUser-Agent: PS5-Medic\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        std::size_t sent = 0;
        while (sent < req.size()) {
            const int w = wait_fd(s.fd, POLLOUT, deadline);
            if (w == 0) {
                r.error = "timeout";
                return r;
            }
            if (w < 0) {
                r.error = "network";
                return r;
            }
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            const ssize_t n = send(s.fd, req.data() + sent, req.size() - sent, flags);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                r.error = "network";
                return r;
            }
            sent += static_cast<std::size_t>(n);
        }

        // Antwort lesen: bis Verbindungsende oder bis der Koerper laut Content-Length komplett ist.
        const std::size_t raw_cap = max_body + 65536;
        std::string raw;
        std::size_t head_end = std::string::npos;
        long content_length = -1;
        bool chunked = false;
        char buf[8192];
        for (;;) {
            const int w = wait_fd(s.fd, POLLIN, deadline);
            if (w == 0) {
                r.error = "timeout";
                return r;
            }
            if (w < 0) {
                r.error = "network";
                return r;
            }
            const ssize_t n = recv(s.fd, buf, sizeof buf, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                r.error = "network";
                return r;
            }
            if (n == 0) break;
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
                    r.error = "too_large";
                    return r;
                }
            }
            if (!chunked && content_length >= 0 && raw.size() >= head_end + 4 + static_cast<std::size_t>(content_length)) break;
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
