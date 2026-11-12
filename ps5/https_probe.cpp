// SPDX-License-Identifier: GPL-3.0-or-later
// Entwicklungswerkzeug (kein Teil des Produkts): ruft ein paar https-Adressen ueber den PS5-Client ab und schreibt Status, Fehler und
// Groesse nach /data/ps5medic/https_probe.log. Senden: nc <ip> 9021 < build-ps5/ps5medic-https-probe.elf
#include <cstdio>
#include <string>

#include "ps5_http.hpp"

int main() {
    std::FILE* log = std::fopen("/data/ps5medic/https_probe.log", "w");
    if (!log) return 1;
    auto client = medic::make_ps5_http_client();
    for (const char* url : {"https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/README.md", "https://github.com/", "https://example.org/", "http://192.168.1.98:8000/rules.json.sig"}) {
        const medic::HttpResponse r = client->get(url, 1 << 20, 15000);
        std::fprintf(log, "%s -> Status %d, Fehler '%s' (%s), %zu Bytes\n", url, r.status, r.error.c_str(), medic::ps5_https_last_detail().c_str(), r.body.size());
        std::fflush(log);
    }
    std::fclose(log);
    return 0;
}
