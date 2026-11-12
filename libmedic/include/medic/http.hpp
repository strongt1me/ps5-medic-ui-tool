// SPDX-License-Identifier: GPL-3.0-or-later
// Minimaler HTTP-Abruf (GET) fuer den Regel-Download. Bewusst klein:
//  * http:// ueber BSD-Sockets (PC und Konsole); https:// nur, wenn der Client es kann (supports_https): auf der Konsole ueber die System-
//    bibliothek sceHttp2 (ps5/ps5_http.cpp). Die Echtheit sichert die Signatur der heruntergeladenen Datei (signature.hpp), nicht der Kanal
//  * keine Weiterleitungen, kein Cookie, keine Zugangsdaten; gesendet wird nur eine GET-Anfrage mit Host und "PS5-Medic" als Kennung
//  * harte Grenzen fuer Groesse und Zeit
#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace medic {

struct HttpResponse {
    int status = 0;     // 0 = keine Antwort
    std::string body;
    // Leer bei einer vollstaendigen HTTP-Antwort (auch bei Status 404). Sonst eine Kennung:
    // bad_url | https_unsupported | resolve | connect | timeout | network | redirect | too_large | bad_response
    std::string error;
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    // Holt `url` (http://host[:port]/pfad). Antworten mit Status 3xx werden nicht verfolgt (error = "redirect").
    // `max_body`: mehr Nutzdaten als das bricht ab (error = "too_large"). `timeout_ms`: Gesamtzeit je Abruf.
    virtual HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) = 0;
    // Versteht der Client auch https://? (Standard: nein; dann lehnt der Download eine https-Adresse mit https_unsupported ab.)
    virtual bool supports_https() const { return false; }
};

struct HttpUrl {
    std::string host;
    int port = 80;
    bool https = false;  // https://, Standardport 443
    std::string path = "/";  // mit fuehrendem Schraegstrich, ohne Fragment
};

// false bei allem ausser http://host[:port][/pfad]; `error` bekommt dann bad_url oder https_unsupported.
bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error);
// Wie parse_http_url, nimmt mit allow_https auch https://host[:port][/pfad] an (out.https, Standardport 443).
bool parse_url(const std::string& url, HttpUrl& out, std::string& error, bool allow_https);

// Haengt einen Dateinamen an eine Basisadresse an: ("http://h:8000/regeln", "rules.json") -> ".../regeln/rules.json".
std::string join_url(const std::string& base, const std::string& name);

// Echter Client auf BSD-Sockets (PC und Konsole). Nicht thread-sicher gedacht, aber ohne gemeinsamen Zustand.
std::unique_ptr<IHttpClient> make_posix_http_client();

}  // namespace medic
