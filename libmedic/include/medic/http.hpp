// SPDX-License-Identifier: GPL-3.0-or-later
// Minimaler HTTP-Abruf (GET) fuer den Regel-Download. Bewusst klein:
//  * http:// und https:// ueber BSD-Sockets (PC und Konsole); TLS mit BearSSL (third_party/bearssl) und eingebauten Wurzelzertifikaten
//    (libmedic/src/tls_roots.c). Die Echtheit der Regeln sichert in jedem Fall die Signatur der heruntergeladenen Datei (siehe
//    signature.hpp), nicht der Kanal.
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
    // bad_url | resolve | connect | timeout | network | tls | redirect | too_large | bad_response
    std::string error;
    std::string detail;  // technische Angabe zum Fehler (z. B. "BearSSL 62 (...)"), nur zur Anzeige; leer, wenn es keine gibt
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    // Holt `url` (http://host[:port]/pfad). Antworten mit Status 3xx werden nicht verfolgt (error = "redirect").
    // `max_body`: mehr Nutzdaten als das bricht ab (error = "too_large"). `timeout_ms`: Gesamtzeit je Abruf.
    virtual HttpResponse get(const std::string& url, std::size_t max_body, int timeout_ms) = 0;
};

struct HttpUrl {
    std::string host;
    int port = 80;           // bei https 443
    bool tls = false;        // https://
    std::string path = "/";  // mit fuehrendem Schraegstrich, ohne Fragment
};

// Standardadresse des Regel-Downloads, wenn config.ini keine update_url hat: die signierten Dateien im oeffentlichen Repository
// (raw.githubusercontent.com liefert ohne Weiterleitung). Die Signatur entscheidet, nicht die Quelle.
constexpr const char* kDefaultUpdateUrl = "https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/";

// false bei allem ausser http(s)://host[:port][/pfad]; `error` bekommt dann bad_url.
bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error);

// Haengt einen Dateinamen an eine Basisadresse an: ("http://h:8000/regeln", "rules.json") -> ".../regeln/rules.json".
std::string join_url(const std::string& base, const std::string& name);

// Echter Client auf BSD-Sockets (PC und Konsole), http:// und https://. Nicht thread-sicher gedacht, aber ohne gemeinsamen Zustand.
std::unique_ptr<IHttpClient> make_posix_http_client();

}  // namespace medic
