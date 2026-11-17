// SPDX-License-Identifier: GPL-3.0-or-later
// Entwicklungswerkzeug (kein Teil des Produkts): fuehrt den Regel-Update-Ablauf der App auf der Konsole ohne Bildschirm aus und schreibt
// das Ergebnis nach /data/ps5medic/update_probe.log: Download von update_url (config.ini), Pruefung der Signatur und, wenn die Datei
// /data/ps5medic/update_probe_install existiert, die Installation (mit Backup, wie ✕ im Bildschirm Regel-Update).
// Senden: nc <ip> 9021 < build-ps5/ps5medic-update-probe.elf
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <string>

#include "medic/http.hpp"
#include "medic/rule_download.hpp"
#include "medic/rule_update.hpp"
#include "medic/rules.hpp"
#include "ps5_http.hpp"
#include "ps5_platform.hpp"

int main() {
    std::FILE* log = std::fopen("/data/ps5medic/update_probe.log", "w");
    if (!log) return 1;
    auto platform = medic::make_ps5_platform(/*writable=*/true);
    auto http = medic::make_ps5_http_client();
    const std::string url = medic::effective_update_url(platform->fs());
    std::fprintf(log, "update_url: %s\n", url.c_str());

    std::string current = "?";
    try {
        current = medic::load_rules_from_file("/data/ps5medic/knowledge/rules.json").db_version;
    } catch (const std::exception& e) {
        std::fprintf(log, "aktive Regeln nicht lesbar: %s\n", e.what());
    }
    std::fprintf(log, "aktive Regeln: %s\n", current.c_str());

    char ts[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
    std::strftime(ts, sizeof ts, "%Y%m%dT%H%M%SZ", &tm);

    const medic::RuleDownloadResult d = medic::download_rule_update(*platform, *http, url, ts);
    std::fprintf(log, "Download: ok=%d Fehler='%s' Datei='%s' Status=%d\n", d.ok ? 1 : 0, d.error.c_str(), d.file.c_str(), d.http_status);
    std::fflush(log);

    const medic::RuleUpdateOffer o = medic::check_rule_update(platform->fs(), current);
    std::fprintf(log, "Pruefung: Zustand=%d Grund='%s' alt=%s neu=%s Regeln=%zu Echtheit='%s' Schluessel=%s\n", static_cast<int>(o.state), o.reason.c_str(),
                 o.old_version.c_str(), o.new_version.c_str(), o.new_rule_count, o.auth.c_str(), o.signed_by.c_str());
    std::fflush(log);

    struct stat st;
    if (o.state == medic::RuleUpdateOffer::State::Ready && ::stat("/data/ps5medic/update_probe_install", &st) == 0) {
        const medic::RuleUpdateResult r = medic::install_rule_update(*platform, current, ts);
        std::fprintf(log, "Installation: ok=%d Fehler='%s' neue Version=%s Backup='%s'\n", r.ok ? 1 : 0, r.error.c_str(), r.new_version.c_str(), r.backup_path.c_str());
    } else {
        std::fprintf(log, "Installation: nicht ausgefuehrt\n");
    }
    std::fclose(log);
    return 0;
}
