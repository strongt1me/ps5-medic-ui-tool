// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP-Client der Konsole: http:// laeuft wie bisher ueber BSD-Sockets (make_posix_http_client), https:// ueber das TLS des Systems
// (libSceHttp2 + libSceSsl, wie das Beispiel http2_get im ps5-payload-sdk). So braucht PS5 Medic keine eigene TLS-Bibliothek; die
// Zertifikate prueft das System. Die Echtheit der Regeln sichert unabhaengig davon die Ed25519-Signatur der Datei.
//
// Wie der POSIX-Client: nur GET mit der Kennung "PS5-Medic", keine Weiterleitungen (Status 3xx -> error "redirect"), keine Cookies,
// keine Zugangsdaten, harte Grenzen fuer Groesse und Zeit. Zusaetzliche Fehlerkennung: "tls" (Zertifikat oder Handshake abgelehnt).
// Fehler aus dem System stehen mit Aufrufname und Code in HttpResponse::detail ("sceHttp2SendRequest 0x80...").
#pragma once
#include "medic/http.hpp"

#include <memory>

namespace medic {

std::unique_ptr<IHttpClient> make_ps5_http_client();

}  // namespace medic
