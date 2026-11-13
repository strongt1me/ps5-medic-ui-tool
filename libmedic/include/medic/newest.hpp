// SPDX-License-Identifier: GPL-3.0-or-later
// Wissensdatei von mehreren Orten laden: es gilt die Datei mit der hoechsten db_version (Gleichstand: der frueheste Ort in der Liste).
// So schlaegt eine heruntergeladene, signierte Datei die mitgelieferte, eine veraltete Kopie von frueher aber nicht die neue Lieferung
// (Konsolentest 13. Nov. 2026: eine alte Fehlercode-Datei mit 40 Codes verdeckte die neue mit 153). Nur Header, damit der PC-Test es pruefen kann.
#pragma once
#include "medic/rules.hpp"  // compare_versions

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

}  // namespace medic
