// SPDX-License-Identifier: GPL-3.0-or-later
// Wissensdatei von mehreren Orten laden: es gilt die Datei mit der hoechsten db_version (Gleichstand: der frueheste Ort in der Liste).
// So schlaegt eine heruntergeladene, signierte Datei die mitgelieferte, eine veraltete Kopie von frueher aber nicht die neue Lieferung
// (Konsolentest 13. Nov. 2026: eine alte Fehlercode-Datei mit 40 Codes verdeckte die neue mit 153). Nur Header, damit der PC-Test es pruefen kann.
#pragma once
#include "medic/rules.hpp"  // compare_versions

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

namespace medic {

// Set braucht das Feld db_version; load(pfad) liefert ein Set oder wirft. Fehlende Dateien werden uebersprungen, ungueltige in `errors`
// vermerkt (Pfad und Meldung). `chosen` bekommt den Pfad der gewaehlten Datei (leer: keine brauchbare gefunden, dann kommt ein leeres Set).
template <class Set, class Loader>
Set load_newest(const std::vector<std::string>& paths, Loader load, std::string* chosen, std::vector<std::string>* errors = nullptr) {
    Set best{};
    std::string best_path;
    for (const std::string& path : paths) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        try {
            Set s = load(path);
            if (best_path.empty() || compare_versions(s.db_version, best.db_version) > 0) {
                best = std::move(s);
                best_path = path;
            }
        } catch (const std::exception& e) {
            if (errors) errors->push_back(path + ": " + e.what());
        }
    }
    if (chosen) *chosen = best_path;
    return best;
}

// Wie load_newest, aber ohne jede Datei vollstaendig zu pruefen: peek(pfad) liefert nur die db_version (JSON lesen, sonst nichts), danach wird nur die Datei mit der
// hoechsten Version geladen. Schlaegt deren Pruefung fehl, kommt die naechstniedrigere dran. Grund: auf der Konsole kostete das Pruefen der veralteten Kopie
// (Regeln 286 ms, Fehlercodes 217 ms, Messung 14. Nov. 2026) mehr als die Haelfte der Ladezeit, und das Ergebnis wurde verworfen.
// Eine Datei, die sich nicht einmal als JSON mit db_version lesen laesst, wird in `errors` gemeldet und uebersprungen.
template <class Set, class Loader, class Peek>
Set load_newest_peek(const std::vector<std::string>& paths, Loader load, Peek peek, std::string* chosen, std::vector<std::string>* errors = nullptr) {
    struct Candidate {
        std::string path, version;
    };
    std::vector<Candidate> found;
    for (const std::string& path : paths) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        try {
            found.push_back({path, peek(path)});
        } catch (const std::exception& e) {
            if (errors) errors->push_back(path + ": " + e.what());
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return compare_versions(a.version, b.version) > 0; });  // Gleichstand: frueherer Ort bleibt vorn
    if (chosen) chosen->clear();
    for (const Candidate& c : found) {
        try {
            Set s = load(c.path);
            if (chosen) *chosen = c.path;
            return s;
        } catch (const std::exception& e) {
            if (errors) errors->push_back(c.path + ": " + e.what());
        }
    }
    return Set{};
}

}  // namespace medic
