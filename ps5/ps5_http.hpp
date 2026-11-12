// SPDX-License-Identifier: GPL-3.0-or-later
// HTTP-Client der Konsole: http:// ueber BSD-Sockets wie am PC, https:// ueber die System-Bibliothek sceHttp2 (TLS der Konsole).
#pragma once
#include <memory>

#include "medic/http.hpp"

namespace medic {

std::unique_ptr<IHttpClient> make_ps5_http_client();

// Einzelheit zum letzten Fehler eines https-Abrufs (Schritt und Rueckgabewert der System-Bibliothek), fuer das Protokoll; sonst leer.
const std::string& ps5_https_last_detail();

}  // namespace medic
