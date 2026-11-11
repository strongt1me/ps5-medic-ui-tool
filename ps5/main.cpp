// SPDX-License-Identifier: GPL-3.0-or-later
// PS5 Medic -- Diagnose-Payload (Phase 2).
// Laeuft als ELF ueber elfldr/Payload Manager auf der Konsole. Tut ausschliesslich Folgendes:
//   1. Zustand sammeln (nur lesen) und die Regeln auswerten
//   2. Befunde auf stdout und in den Kernel-Log (klogsrv) schreiben
//   3. Bericht nach /data/ps5medic/last_check.txt und den Zustand als Snapshot nach
//      /data/ps5medic/snapshots/<zeitstempel>/ schreiben (per FTP holen, am PC mit `medic-cli check` pruefen)
// Es werden KEINE Reparaturen ausgefuehrt. Geschrieben wird nur unter /data/ps5medic/.
#include "ps5_platform.hpp"

#include "medic/collectors.hpp"
#include "medic/rules.hpp"
#include "medic/selftest.hpp"
#include "medic/snapshot_export.hpp"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include <ps5/klog.h>

namespace {

constexpr const char* kBaseDir = "/data/ps5medic";
// Erste vorhandene Datei gewinnt (per `make deploy` abgelegt).
const char* const kRulesCandidates[] = {"/data/ps5medic/knowledge/rules.json", "/data/homebrew/PS5MEDIC/rules.json"};

std::string g_report;

void out(const std::string& line) {
    g_report += line + "\n";
    std::puts(line.c_str());
    klog_puts((line + "\n").c_str());
}

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

const char* label(medic::Severity s) {
    return s == medic::Severity::Critical ? "KRITISCH" : s == medic::Severity::Warning ? "WARNUNG" : "HINWEIS";
}

}  // namespace

int main() {
    out("PS5 Medic - Check-up (Diagnose-Payload, nur lesend)");
    auto platform = medic::make_ps5_platform(/*writable=*/false);
    medic::Facts facts = medic::collect_all(*platform);

    out("Firmware: " + platform->sys().firmware() + "  Modell: " + platform->sys().model());
    out("Konsolenstart (Epoch): " + std::to_string(platform->sys().boot_epoch()));
    for (const char* k : {"smp.version", "net.port_9021", "jb.seconds_after_boot", "jb.first_process", "ps5theme.version", "apps.oversized", "smp.api.bind", "smp.api.port", "smp.api.state", "sys.temp.cpu_c", "sys.temp.soc_c", "boot.unclean_total", "boot.unclean_14d", "boot.log_last", "errors.total", "db.count", "savedata.current_account", "games.case_mismatch"})
        if (const auto* v = facts.get(k)) out(std::string("Fakt ") + k + ": " + v->dump());
    if (const auto* dbs = facts.get("db.files"); dbs && dbs->is_array())  // Datenbank-Pruefung je Datei: Pruefung 1 = ok, 0 = Fehler, -1 = nicht geprueft
        for (const auto& e : *dbs) out("  db " + e.value("path", std::string()) + "  Groesse " + e["size"].dump() + "  Pruefung " + e["integrity"].dump() + (e["integrity_skip"].get<std::string>().empty() ? std::string() : "  (nicht geprueft: " + e["integrity_skip"].get<std::string>() + ")"));
    out("Prozesse:");
    for (const auto& n : platform->procs().running()) out("  " + n);

    // Selbsttest (nur lesend, ohne Marker: die Plattform ist hier nicht beschreibbar): jeder Test genau einmal, Ergebnis als Zeile.
    out("Selbsttest:");
    for (const auto& def : medic::selftest_registry()) {
        const medic::TestResult r = medic::run_selftest(*platform, facts, def);
        std::string line = std::string("  ") + def.id + ": " + medic::to_string(r.status) + (r.reason.empty() ? std::string() : " (" + r.reason + ")");
        for (const auto& m : r.measurements) line += ", " + m.name + "=" + std::to_string(m.value) + (m.unit.empty() ? std::string() : " " + m.unit);
        out(line);
    }

    // Regeln laden
    medic::RuleSet rules;
    bool have_rules = false;
    for (const char* path : kRulesCandidates) {
        if (!std::filesystem::exists(path)) continue;
        try {
            rules = medic::load_rules_from_file(path);
            have_rules = true;
            out(std::string("Regeln: ") + rules.db_version + " (" + std::to_string(rules.rules.size()) + ") aus " + path);
        } catch (const std::exception& e) {
            out(std::string("Regeldatei ungueltig (") + path + "): " + e.what());
        }
        break;
    }
    if (!have_rules) out("Keine Regeldatei gefunden (erwartet: /data/ps5medic/knowledge/rules.json) -- nur Snapshot-Export.");

    if (have_rules) {
        auto findings = medic::evaluate(rules, facts);
        std::string untested;
        for (const auto& c : medic::evaluate_coverage(rules, facts))
            if (!c.tested) untested += (untested.empty() ? "" : ", ") + c.text_de;
        if (!untested.empty()) out("Nicht geprueft: " + untested);
        if (findings.empty()) out("Keine Befunde.");
        for (const auto& f : findings) {
            out(std::string("[") + label(f.severity) + "] " + f.rule_id);
            out("  " + f.text_de);
            for (const auto& e : f.evidence) out("  Evidenz: " + e);
            const medic::Confidence conf = medic::effective_confidence(f);
            out("  Quelle: " + f.source + " (" + f.verified + "), Verlaesslichkeit: " + conf.level + "/" + conf.basis);
        }
    }

    // Bericht und Snapshot (nur unter /data/ps5medic/)
    const std::string ts = timestamp();
    std::error_code ec;
    std::filesystem::create_directories(kBaseDir, ec);
    {
        std::ofstream rep(std::string(kBaseDir) + "/last_check.txt", std::ios::binary | std::ios::trunc);
        rep << g_report;
    }
    const std::string snap = std::string(kBaseDir) + "/snapshots/" + ts;
    auto res = medic::export_snapshot(*platform, facts, snap, ts);
    if (res.ok) out("Snapshot: " + snap + " (" + std::to_string(res.entries) + " Eintraege)");
    else out("Snapshot-Export fehlgeschlagen: " + res.error);
    for (const auto& w : res.warnings) out("  Hinweis: " + w);
    return res.ok ? 0 : 1;
}
