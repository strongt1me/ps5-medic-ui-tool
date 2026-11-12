// SPDX-License-Identifier: GPL-3.0-or-later
// https:// auf der Konsole ueber sceHttp2 (System-TLS). Nach dem Beispiel http2_get des ps5-payload-sdk (John Toernblom, GPL-3.0-or-later):
// sceNetInit, Netzpool, sceSslInit, sceHttp2Init, Vorlage, Anfrage, SendRequest, GetStatusCode, ReadData.
//
// Grenzen wie beim Socket-Client (medic/http.hpp): nur GET, keine Weiterleitungen (Status 3xx -> "redirect"), keine Cookies und
// Zugangsdaten, harte Grenze fuer Groesse und Zeit. Das Zertifikat prueft das System; ein Fehler dabei erscheint als "network". Die
// Echtheit der Regeln sichert unabhaengig davon die Ed25519-Signatur der Datei.
#include "ps5_http.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

extern "C" {
int sceNetInit();
int sceNetPoolCreate(const char*, int, int);
int sceSslInit(size_t);
int sceHttp2Init(int, int, size_t, int);
int sceHttp2CreateTemplate(int, const char*, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2SetAutoRedirect(int, int);
int sceHttp2SetTimeOut(int, uint32_t);
int sceHttp2CreateRequestWithURL(int, const char*, const char*, uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void*, size_t);
int sceHttp2GetStatusCode(int, int*);
int sceHttp2ReadData(int, void*, size_t);
}

namespace medic {
namespace {

// Die Bibliotheken werden einmal je Programmlauf aufgesetzt und nie abgebaut (der Download ist selten; Abbau hat auf der Konsole
// keinen Nutzen und kann haengen).
std::string g_detail;
void note(const char* step, int rc) {
    char b[64];
    std::snprintf(b, sizeof b, "%s=0x%08x", step, static_cast<unsigned>(rc));
    g_detail = b;
}

struct Http2Context {
    int http = -1;
    bool tried = false;
};

Http2Context& context() {
    static Http2Context c;
    return c;
}

bool init_http2() {
    Http2Context& c = context();
    if (c.http >= 0) return true;
    if (c.tried) return false;
    c.tried = true;
    if (const int rc = sceNetInit(); rc != 0) { note("sceNetInit", rc); return false; }
    const int net = sceNetPoolCreate("ps5medic_http", 32 * 1024, 0);
    if (net < 0) { note("sceNetPoolCreate", net); return false; }
    const int ssl = sceSslInit(256 * 1024);
    if (ssl < 0) { note("sceSslInit", ssl); return false; }
    const int http = sceHttp2Init(net, ssl, 256 * 1024, 1);
    if (http < 0) { note("sceHttp2Init", http); return false; }
    c.http = http;
    return true;
}

class Ps5HttpClient final : public IHttpClient {
public:
    Ps5HttpClient() : plain_(make_posix_http_client()) {}

    bool supports_https() const override { return true; }

    HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) override {
        HttpUrl parsed;
        std::string err;
        if (!parse_url(url, parsed, err, true)) {
            HttpResponse r;
            r.error = err;
            return r;
        }
        if (!parsed.https) return plain_->get(url, max_body, timeout_ms);
        return get_https(url, max_body, timeout_ms);
    }

private:
    HttpResponse get_https(const std::string& url, std::size_t max_body, int timeout_ms) {
        HttpResponse r;
        if (!init_http2()) {
            r.error = "network";
            return r;
        }
        const int tmpl = sceHttp2CreateTemplate(context().http, "PS5-Medic", 3, 1);
        if (tmpl < 0) {
            note("CreateTemplate", tmpl);
            r.error = "network";
            return r;
        }
        sceHttp2SetAutoRedirect(tmpl, 0);  // keine Weiterleitungen (siehe oben)
        sceHttp2SetTimeOut(tmpl, static_cast<uint32_t>(timeout_ms) * 1000u);
        const int req = sceHttp2CreateRequestWithURL(tmpl, "GET", url.c_str(), 0);
        if (req < 0) {
            note("CreateRequest", req);
            sceHttp2DeleteTemplate(tmpl);
            r.error = "bad_url";
            return r;
        }
        if (const int rc = sceHttp2SendRequest(req, nullptr, 0); rc != 0) {
            note("SendRequest", rc);
            r.error = "network";
        } else {
            int status = 0;
            if (sceHttp2GetStatusCode(req, &status) != 0) {
                r.error = "bad_response";
            } else {
                r.status = status;
                if (status >= 300 && status < 400) {
                    r.error = "redirect";
                } else {
                    char buf[4096];
                    for (;;) {
                        const int n = sceHttp2ReadData(req, buf, sizeof buf);
                        if (n < 0) {
                            r.error = "network";
                            break;
                        }
                        if (n == 0) break;
                        r.body.append(buf, static_cast<std::size_t>(n));
                        if (r.body.size() > max_body) {
                            r.error = "too_large";
                            break;
                        }
                    }
                }
            }
        }
        sceHttp2DeleteRequest(req);
        sceHttp2DeleteTemplate(tmpl);
        if (!r.error.empty()) r.body.clear();
        return r;
    }

    std::unique_ptr<IHttpClient> plain_;
};

}  // namespace

const std::string& ps5_https_last_detail() { return g_detail; }

std::unique_ptr<IHttpClient> make_ps5_http_client() { return std::make_unique<Ps5HttpClient>(); }

}  // namespace medic
