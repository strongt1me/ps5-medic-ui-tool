// SPDX-License-Identifier: GPL-3.0-or-later
// Archiv der Check-ups: Jeder Check-up wird als eigene Datei /data/ps5medic/checks/<Zeit>.json gespeichert (Befunde mit Text, Belegen und
// Reparaturangebot, Firmware, Regelversion, Abdeckung, behobene Regeln und Messwerte). Man kann spaeter einen auswaehlen und ansehen, so wie er
// damals war, und zwei Staende vergleichen. Nichts wird geaendert oder geloescht; eine bestehende Datei wird nie ueberschrieben (bei gleicher
// Zeit bekommt die neue ein Suffix). Die Dateien liegen nur im eigenen Ordner und koennen Pfade und Titelnamen aus den Belegen enthalten
// (wie die Berichte); sie verlassen die Konsole nur, wenn jemand sie abholt. Der Verlauf fuer Vergleiche und Dringlichkeit bleibt history.hpp.
#pragma once
#include "medic/history.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <string>
#include <vector>

namespace medic {

constexpr const char* kChecksDir = "/data/ps5medic/checks";
constexpr std::size_t kChecksListMax = 100;      // so viele juengste Check-ups zeigt die Liste (aelteste bleiben als Dateien)
constexpr std::size_t kCheckMaxBytes = 2u << 20;  // groessere Dateien werden nicht gelesen

struct StoredCheck {
    std::string id;  // Dateiname ohne ".json" (die Zeit, ggf. mit Suffix); beim Speichern leer lassen
    std::string ts, fw, model, rules_version;
    std::vector<Finding> findings;
    std::vector<CoverageResult> coverage;
    std::vector<std::string> resolved;
    std::vector<MetricChange> metrics;
};

struct CheckSummary {
    std::string id, ts, fw, model, rules_version;
    int total = 0, critical = 0, warning = 0, info = 0;
    int untested = 0;  // nicht gepruefte Bereiche
};

// Speichert den Check-up (ohne fs_rw oder mit ungueltiger Zeit: false). Liefert die vergebene id.
bool save_check(Platform& p, const StoredCheck& c, std::string* id = nullptr);

// Die juengsten Check-ups (neueste zuerst, hoechstens kChecksListMax); unlesbare Dateien werden uebersprungen.
std::vector<CheckSummary> list_checks(const IFileSystem& fs);

// Laedt einen Check-up; false bei ungueltiger id, fehlender oder beschaedigter Datei.
bool load_check(const IFileSystem& fs, const std::string& id, StoredCheck& out);

// Vergleich zweier Check-ups nach Regel-ID: neu (nur im neueren), behoben (nur im aelteren), unveraendert (in beiden). Die Befunde stammen
// aus dem Check-up, in dem sie stehen (bei "unveraendert" aus dem neueren).
struct CheckDiff {
    std::vector<Finding> added, resolved, unchanged;
};
CheckDiff compare_checks(const StoredCheck& newer, const StoredCheck& older);

}  // namespace medic
