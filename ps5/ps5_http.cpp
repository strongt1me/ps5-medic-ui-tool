// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps5_http.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

// Schnittstellen der Systembibliotheken (Signaturen wie im SDK-Beispiel samples/http2_get; gelinkt mit -lSceNet -lSceSsl -lSceHttp2)
extern "C" {
int sceNetInit();
int sceNetPoolCreate(const char*, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(size_t);
int sceSslTerm(int);
int sceHttp2Init(int, int, size_t, int);
int sceHttp2Term(int);
int sceHttp2CreateTemplate(int, const char*, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2SetAutoRedirect(int, int);
int sceHttp2SetTimeOut(int, uint32_t);
int sceHttp2SetConnectTimeOut(int, uint32_t);
int sceHttp2SetResolveTimeOut(int, uint32_t);
int sceHttp2SetSendTimeOut(int, uint32_t);
int sceHttp2SetRecvTimeOut(int, uint32_t);
int sceHttp2CreateRequestWithURL(int, const char*, const char*, uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void*, size_t);
int sceHttp2GetStatusCode(int, int*);
int sceHttp2ReadData(int, void*, size_t);
}

namespace medic {

namespace {

using Clock = std::chrono::steady_clock;

std::string code_text(const char* call, int code) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s 0x%08x", call, static_cast<unsigned>(code));
    return buf;
}

// Eine TLS-Sitzung: alles, was angelegt wurde, wird in umgekehrter Reihenfolge wieder freigegeben (auch bei jedem Fehler).
struct Session {
    int net_pool = -1, ssl = -1, http = -1, tmpl = -1, req = -1;
    ~Session() {
        if (req >= 0) sceHttp2DeleteRequest(req);
        if (tmpl >= 0) sceHttp2DeleteTemplate(tmpl);
        if (http >= 0) sceHttp2Term(http);
        if (ssl >= 0) sceSslTerm(ssl);
        if (net_pool >= 0) sceNetPoolDestroy(net_pool);
    }
};

class Ps5HttpClient final : public IHttpClient {
public:
    Ps5HttpClient() : plain_(make_posix_http_client()) {}

    HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) override {
        HttpUrl u;
        HttpResponse r;
        if (!parse_http_url(url, u, r.error)) return r;
        if (!u.tls) return plain_->get(url, max_body, timeout_ms);
        return get_tls(url, max_body, timeout_ms);
    }

private:
    static HttpResponse fail(const char* error, const std::string& detail = {}) {
        HttpResponse r;
        r.error = error;
        r.detail = detail;
        return r;
    }

    // Fehler aus libSceSsl liegen im Bereich 0x8095xxxx (Zertifikat, Handshake, Protokoll): eigene Kennung "tls"; alles andere "network".
    static HttpResponse system_error(const char* call, int code) {
        return fail((static_cast<unsigned>(code) & 0xFFFF0000u) == 0x80950000u ? "tls" : "network", code_text(call, code));
    }

    HttpResponse get_tls(const std::string& url, std::size_t max_body, int timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        const uint32_t usec = static_cast<uint32_t>(timeout_ms) * 1000u;
        static bool net_inited = false;
        if (!net_inited) {  // einmal je Prozess; meldet das System "schon initialisiert", ist das kein Fehler
            sceNetInit();
            net_inited = true;
        }
        Session s;
        if ((s.net_pool = sceNetPoolCreate("ps5medic_http", 32 * 1024, 0)) < 0) {
            const int code = s.net_pool;
            s.net_pool = -1;
            return system_error("sceNetPoolCreate", code);
        }
        if ((s.ssl = sceSslInit(256 * 1024)) < 0) {
            const int code = s.ssl;
            s.ssl = -1;
            return system_error("sceSslInit", code);
        }
        if ((s.http = sceHttp2Init(s.net_pool, s.ssl, 256 * 1024, 1)) < 0) {
            const int code = s.http;
            s.http = -1;
            return system_error("sceHttp2Init", code);
        }
        if ((s.tmpl = sceHttp2CreateTemplate(s.http, "PS5-Medic", 3, 1)) < 0) {
            const int code = s.tmpl;
            s.tmpl = -1;
            return system_error("sceHttp2CreateTemplate", code);
        }
        sceHttp2SetAutoRedirect(s.tmpl, 0);  // Weiterleitungen werden nicht verfolgt (Status 3xx kommt an und wird als "redirect" gemeldet)
        sceHttp2SetTimeOut(s.tmpl, usec);
        sceHttp2SetConnectTimeOut(s.tmpl, usec);
        sceHttp2SetResolveTimeOut(s.tmpl, usec);
        sceHttp2SetSendTimeOut(s.tmpl, usec);
        sceHttp2SetRecvTimeOut(s.tmpl, usec);
        if ((s.req = sceHttp2CreateRequestWithURL(s.tmpl, "GET", url.c_str(), 0)) < 0) {
            const int code = s.req;
            s.req = -1;
            return system_error("sceHttp2CreateRequestWithURL", code);
        }
        if (const int rc = sceHttp2SendRequest(s.req, nullptr, 0); rc < 0) return system_error("sceHttp2SendRequest", rc);
        HttpResponse r;
        int status = 0;
        if (const int rc = sceHttp2GetStatusCode(s.req, &status); rc < 0) return system_error("sceHttp2GetStatusCode", rc);
        r.status = status;
        if (status >= 300 && status < 400) {
            r.error = "redirect";
            return r;
        }
        char buf[8192];
        for (;;) {
            if (Clock::now() > deadline) {
                r.error = "timeout";
                return r;
            }
            const int n = sceHttp2ReadData(s.req, buf, sizeof buf);
            if (n < 0) {
                HttpResponse e = system_error("sceHttp2ReadData", n);
                e.status = status;
                return e;
            }
            if (n == 0) break;
            if (r.body.size() + static_cast<std::size_t>(n) > max_body) {
                r.body.clear();
                r.error = "too_large";
                return r;
            }
            r.body.append(buf, static_cast<std::size_t>(n));
        }
        return r;
    }

    std::unique_ptr<IHttpClient> plain_;
};

}  // namespace

std::unique_ptr<IHttpClient> make_ps5_http_client() { return std::make_unique<Ps5HttpClient>(); }

}  // namespace medic
