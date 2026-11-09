// SPDX-License-Identifier: GPL-3.0-or-later
// Regel-Download: holt rules.json und rules.json.sig von einer Adresse in den Update-Ordner. Das ist die einzige Funktion
// von PS5 Medic, die das Netz benutzt. Sie laeuft nur auf ausdruecklichen Befehl des Nutzers, nur zu der Adresse, die er selbst
// in /data/ps5medic/config.ini eingetragen hat (update_url=http://...), und sendet nichts ausser einer GET-Anfrage.
//
// Danach versucht er dasselbe fuer error_codes.json und error_codes.json.sig (Fehlercode-Nachschlagewerk). Das ist optional: bietet
// der Server die Dateien nicht an (404), bleibt es beim Regel-Update; scheitert nur dieser Teil, gilt der Regel-Download trotzdem
// (siehe codes_error). Es sind also hoechstens vier GET-Abrufe, alle zur selben Adresse.
//
// Der Download prueft nichts und installiert nichts: er legt nur die beiden Dateien nach update/ (bereits dort liegende
// werden nach update/replaced/<zeit>/ geschoben, nie geloescht). Echtheit, Schema und Version prueft danach check_rule_update;
// installiert wird wie immer erst nach Bestaetigung (rule_update.hpp).
#pragma once
#include "medic/http.hpp"
#include "medic/platform.hpp"

#include <string>

namespace medic {

constexpr const char* kConfigPath = "/data/ps5medic/config.ini";
constexpr std::size_t kMaxSigBytes = 4096;

// Wert von `update_url` aus config.ini (Zeilen "schluessel=wert", '#' und ';' leiten Kommentare ein). Leer, wenn nichts steht.
std::string configured_update_url(const IFileSystem& fs);

struct RuleDownloadResult {
    bool ok = false;
    // Kennung: no_url | bad_url | https_unsupported | no_write | bad_timestamp | write_failed
    //          | http_status (Server antwortete nicht mit 200; siehe http_status und file)
    //          | net_<fehler> mit den Fehlern aus HttpResponse::error (net_connect, net_timeout, net_too_large, ...)
    std::string error;
    int http_status = 0;
    std::string file;  // "rules.json" oder "rules.json.sig": bei welcher Datei es scheiterte
    std::size_t bytes = 0;  // bei Erfolg: Groesse der Regeldatei
    // Fehlercodes (optional): codes_ok = beide Dateien liegen jetzt im Update-Ordner. Bietet der Server sie nicht an, bleibt alles
    // leer; sonst steht in codes_error, warum nur dieser Teil scheiterte (http_status, net_<fehler>, write_failed) und in codes_file die Datei.
    bool codes_ok = false;
    std::string codes_error;
    std::string codes_file;
};

RuleDownloadResult download_rule_update(Platform& p, IHttpClient& http, const std::string& base_url, const std::string& timestamp);

}  // namespace medic
