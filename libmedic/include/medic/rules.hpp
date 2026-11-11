// SPDX-License-Identifier: GPL-3.0-or-later
// Regel-Engine: laedt knowledge/rules.json, prueft sie und wertet sie gegen Fakten aus.
#pragma once
#include "medic/facts.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace medic {

enum class Severity { Critical, Warning, Info };
const char* to_string(Severity s);

// Wie sicher ein Befund ist und worauf die Aussage ruht (docs/DIAGNOSE.md, Leitregel 3: Evidenz trennen).
// `level`: high | medium | low. `basis`: observed (an der Konsole gelesen oder gemessen, ohne Annahme ueber die Ursache),
// doc (dokumentiert), community (Bericht aus der Gemeinschaft), derived (aus Beobachtung oder Bericht abgeleitet),
// unknown (nicht bestaetigt). Ohne Angabe in der Regel gilt der Standard aus `verified`; eine Abweichung braucht einen Grund.
struct Confidence {
    std::string level;
    std::string basis;
    std::string reason_de, reason_en;  // optional, nur bei ausdruecklicher Angabe in der Regel
};
Confidence default_confidence(const std::string& verified);  // doc: high/doc, community: medium/community, sonst: low/unknown

struct Rule {
    std::string id;
    Severity severity = Severity::Info;
    nlohmann::json when;
    std::string text_de, text_en;
    std::string evidence_path;  // optional
    std::string evidence_filter;  // optional: Regex, nur passende Array-Elemente (z. B. Log-Zeilen) als Evidenz
    std::string fix;            // leer = kein Fix
    nlohmann::json fix_params = nlohmann::json::object();  // Parameter fuer den Fix (siehe fixes.hpp)
    std::string source;
    std::string verified;       // doc | community | todo
    Confidence confidence;      // Standard aus `verified`, siehe default_confidence
    std::string applies_fw;     // optional, z. B. ">=7.00 <=13.60"
};

struct Finding {
    std::string rule_id;
    Severity severity = Severity::Info;
    std::string text_de, text_en;
    std::vector<std::string> evidence;
    std::string evidence_path;  // Fakt, aus dem die Evidenz stammt (Beschriftung im Bericht)
    std::string fix;
    nlohmann::json fix_params = nlohmann::json::object();
    std::string source;
    std::string verified;
    Confidence confidence;  // leer: Standard aus `verified` (effective_confidence)
};

// Verlaesslichkeit des Befunds; fehlende Felder (Befunde aus Tests oder alten Regeldateien) kommen aus `verified`.
Confidence effective_confidence(const Finding& f);

// Ein Pruefbereich der App. `tested_when` ist eine Bedingung wie bei den Regeln (gegen die Fakten): trifft sie zu, lagen fuer den Bereich
// lesbare Daten vor und er wurde geprueft; sonst gilt er als nicht geprueft. "Keine Befunde" heisst fuer einen nicht gepruefte Bereich
// nie "in Ordnung" (docs/DIAGNOSE.md, Leitregel 2).
struct CoverageItem {
    std::string id;
    std::string text_de, text_en;  // Name des Bereichs
    nlohmann::json tested_when;
};

struct RuleSet {
    std::string db_version;
    std::vector<Rule> rules;
    std::vector<CoverageItem> coverage;  // optional
};

struct CoverageResult {
    std::string id, text_de, text_en;
    bool tested = false;
};

// Wirft std::runtime_error mit verstaendlicher Meldung, wenn JSON oder Schema ungueltig sind.
RuleSet load_rules_from_string(const std::string& json_text);
RuleSet load_rules_from_file(const std::string& path);

// Ergebnis nach Schweregrad (kritisch zuerst), dann nach Regel-ID sortiert.
std::vector<Finding> evaluate(const RuleSet& rs, const Facts& facts);

// Welche Pruefbereiche waren gedeckt, in der Reihenfolge der Regeldatei. Ohne Abdeckungsliste in der Regeldatei: leer.
std::vector<CoverageResult> evaluate_coverage(const RuleSet& rs, const Facts& facts);

// Hilfen (auch fuer Tests)
int compare_versions(const std::string& a, const std::string& b);  // <0, 0, >0
bool fw_in_range(const std::string& fw, const std::string& range);

}  // namespace medic
