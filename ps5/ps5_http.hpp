// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP-Client der Konsole: derselbe Client wie am PC (make_posix_http_client): http:// und https:// ueber BSD-Sockets, TLS mit BearSSL
// (third_party/bearssl) und eingebauten Wurzelzertifikaten (libmedic/src/tls_roots.c). Frueher lief https:// ueber das TLS des Systems
// (libSceHttp2); die Konsole lehnte dort die Kette von GitHub ab (0x8095f00c), und die Systembibliothek liess sich ohne Header nicht
// einstellen. Die Echtheit der Regeln sichert unabhaengig vom Kanal die Ed25519-Signatur der Datei.
//
// Nur GET mit der Kennung "PS5-Medic", keine Weiterleitungen (Status 3xx -> error "redirect"), keine Cookies, keine Zugangsdaten,
// harte Grenzen fuer Groesse und Zeit. Fehlerkennung "tls" bei abgelehntem Zertifikat oder Handshake (Einzelheit in HttpResponse::detail).
#pragma once
#include "medic/http.hpp"

#include <memory>

namespace medic {

std::unique_ptr<IHttpClient> make_ps5_http_client();

}  // namespace medic
