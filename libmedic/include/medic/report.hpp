// SPDX-License-Identifier: GPL-3.0-or-later
// Bericht: Markdown fuer Foren/Issues, Kurzfassung fuer den QR-Code, Anonymisierung.
// Alle Texte, die diese Funktionen liefern, sind bereits anonymisiert (IP-/MAC-Adressen, Benutzer- und Konto-IDs,
// Seriennummern werden durch Platzhalter ersetzt). Dateinamen und Pfade bleiben erhalten: sie sind meist die Evidenz.
#pragma once
#include "medic/facts.hpp"
#include "medic/fixes.hpp"
#include "medic/history.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"
#include "medic/selftest.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace medic {

constexpr const char* kMedicVersion = "0.4.0-dev";
constexpr const char* kReportDir = "/data/ps5medic/reports";

enum class ReportLang { De, En };

// Eine Gruppe gleichartiger Fehlerzeilen aus dem Log (Zahlen und Kennungen sind beim Gruppieren ausgeblendet).
struct UnknownLine {
    std::string example;  // erste Zeile dieser Gruppe, unveraendert
    int count = 0;
};

struct ReportInput {
    std::string created_utc;     // "YYYYMMDDTHHMMSSZ" (so wie der Zeitstempel fuer Journal und Backups)
    std::string rules_version;
    std::size_t rule_count = 0;
    std::vector<Finding> findings;
    std::vector<CoverageResult> coverage;  // Pruefbereiche (nicht gepruefte stehen im Bericht)
    std::vector<std::string> resolved;     // seit der letzten Pruefung behobene Regeln (Verlauf)
    std::vector<MetricChange> metrics;     // Messwerte mit Veraenderung (Verlauf)
    std::vector<TestResult> selftests;     // Ergebnis des letzten Selbsttests (leer: keiner gelaufen)
    std::string selftest_finished;
    Facts facts;                 // Umgebung: Firmware, Modell, Versionen, Prozesse, Speicher
    std::vector<TxnInfo> history;
    std::vector<UnknownLine> unknown_lines;  // fehlerartige Logzeilen ohne passenden Befund
    std::string saved_path;      // wo der volle Bericht liegt (leer = nicht gespeichert)
    // Fehlercode, den der Nutzer im Nachschlagewerk fuer den Bericht vermerkt hat (leer = keiner), mit Titel in beiden Sprachen.
    std::string error_code, error_title_de, error_title_en;
};

// Fehlerartige Zeilen im ShadowMount+-Log (Fakt smp.log.lines), die zu keinem Befund gehoeren: Stoff fuer neue Regeln.
// Erkannt werden nur eindeutige Fehlerwoerter (failed, panic, cannot, denied, timed out ...), "error" mit Wert ungleich 0
// und das grossgeschriebene ERROR; ein blosses "error" im Text (z. B. "game error monitoring") oder in einem Spieltitel
// zaehlt nicht. Gleichartige Zeilen (nur Zeit, Zahlen oder Kennungen verschieden) bilden eine Gruppe; haeufigste zuerst.
std::vector<UnknownLine> unknown_error_lines(const Facts& facts, const std::vector<Finding>& findings, std::size_t max_groups = 10);

// Stellt die Berichtsdaten zusammen (Verlauf der Reparaturen kommt aus dem Journal der Plattform).
ReportInput make_report_input(const Platform& p, const Facts& facts, const RuleSet& rules, std::vector<Finding> findings,
                              const std::string& timestamp);

// Ersetzt IPv4-/IPv6-/MAC-Adressen, Benutzer- und Konto-IDs ("/user/home/<8 Hex>", 16-stellige Hex-Kennungen,
// "serial: ...", "account id: ..." u. ae.) durch Platzhalter wie [IP], [MAC], [ID], [ACCOUNT-ID], [SERIAL].
std::string anonymize(const std::string& text);

// Voller Bericht als Markdown.
std::string build_report(const ReportInput& in, ReportLang lang);

// Kurzfassung als reiner Text (ASCII) fuer den QR-Code, hoechstens `max_bytes` Byte. Wird an Befund-Grenzen
// gekuerzt; `truncated` meldet, ob Befunde weggelassen wurden.
std::string build_report_compact(const ReportInput& in, ReportLang lang, std::size_t max_bytes, bool* truncated = nullptr);

// Dateiname "report_<zeitstempel>.md" im Berichtsordner; leer, wenn der Zeitstempel ungueltig ist.
std::string report_path_for(const std::string& timestamp);

}  // namespace medic
