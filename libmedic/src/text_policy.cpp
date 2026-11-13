// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/text_policy.hpp"

#include <initializer_list>
#include <regex>
#include <string>
#include <stdexcept>
#include <vector>

namespace medic {
namespace {

using json = nlohmann::json;

// Alles ohne Gross-/Kleinschreibung. "(?:[^.\n]|\.[0-9]){0,80}": derselbe Satz, damit der Menuepfad "System Software > Reset Options ..." und
// ein spaeterer, getrennter Satz ueber ein Spiel-Update nicht zusammenfallen.
const std::vector<std::regex>& patterns() {
    static const std::vector<std::regex> p = [] {
        const auto flags = std::regex::ECMAScript | std::regex::icase;
        return std::vector<std::regex>{
            std::regex(R"(system\s*-?\s*(update|upgrade))", flags),                                            // Systemupdate, System-Update, system update
            std::regex(R"(firmware\s*-?\s*(update|upgrade|aktualisier))", flags),                              // Firmware-Update, Firmware aktualisieren
            std::regex(R"((system\s*software|systemsoftware|firmware)(?:[^.\n]|\.[0-9]){0,80}(aktualisier|update|upgrade))", flags),  // Systemsoftware ... aktualisieren
            std::regex(R"((aktualisier|update|upgrade)(?:[^.\n]|\.[0-9]){0,80}(system\s*software|systemsoftware|firmware))", flags),  // update the system software
            std::regex(R"((neueste|neuste|latest|newest)\s+(system\s*software|systemsoftware|firmware))", flags),         // neueste Systemsoftware
            // Franzoesisch: "mise a jour / mettre a jour / actualiser ... micrologiciel / firmware / logiciel systeme" (und umgekehrt)
            std::regex(R"((mise\s+(?:\xc3\xa0|a)\s+jour|mettre\s+(?:\xc3\xa0|a)\s+jour|actualis\w*)(?:[^.\n]|\.[0-9]){0,80}(micrologiciel|firmware|logiciel\s+syst))", flags),
            std::regex(R"((micrologiciel|firmware|logiciel\s+syst)(?:[^.\n]|\.[0-9]){0,80}(mise\s+(?:\xc3\xa0|a)\s+jour|mettre\s+(?:\xc3\xa0|a)\s+jour|actualis\w*))", flags),
            // Italienisch: "aggiornare / aggiornamento ... firmware / software di sistema" (und umgekehrt)
            std::regex(R"((aggiorn\w*)(?:[^.\n]|\.[0-9]){0,80}(firmware|software\s+di\s+sistema))", flags),
            std::regex(R"((firmware|software\s+di\s+sistema)(?:[^.\n]|\.[0-9]){0,80}(aggiorn\w*))", flags),
            // Spanisch: "actualizar / actualizacion ... firmware / software del sistema" (und umgekehrt)
            std::regex(R"((actualiz\w*)(?:[^.\n]|\.[0-9]){0,80}(firmware|software\s+del\s+sistema))", flags),
            std::regex(R"((firmware|software\s+del\s+sistema)(?:[^.\n]|\.[0-9]){0,80}(actualiz\w*))", flags),
            // "Systemaktualisierung", "aggiornamento di/del sistema", "actualizacion del sistema", "mise a jour du systeme" (ohne "software"/"firmware" dahinter)
            std::regex(R"(\bsystem\s*-?\s*aktualisier|\baktualisierung\s+(des\s+)?systems\b)", flags),
            std::regex(R"(aggiornamento\s+(di|del)\s+sistema)", flags),
            std::regex(R"(actualizaci(?:o|\xc3\xb3)n\s+del\s+sistema)", flags),
            std::regex(R"((mise\s+(?:\xc3\xa0|a)\s+jour|mettre\s+(?:\xc3\xa0|a)\s+jour)\s+du\s+syst(?:\xc3\xa8|e)me(?!\s+de\s+fichiers))", flags),
        };
    }();
    return p;
}

void walk(const json& j, const std::string& path, const std::string& what) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        if ((s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0) && s.find_first_of(" \t\n") == std::string::npos) return;  // reine Adresse
        const std::string hit = find_system_update_advice(s);
        if (!hit.empty()) throw std::runtime_error(what + ": " + path + ": Hinweis auf ein Update der Systemsoftware/Firmware ist nicht erlaubt: \"" + hit + "\"");
    } else if (j.is_array()) {
        for (std::size_t i = 0; i < j.size(); ++i) walk(j[i], path + "[" + std::to_string(i) + "]", what);
    } else if (j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!it.key().empty() && it.key()[0] == '_') continue;  // Kommentarfelder der Dateien
            walk(it.value(), path.empty() ? it.key() : path + "." + it.key(), what);
        }
    }
}

// Notwendige Bedingung fuer jedes der Muster oben: ein Hauptwort (firmware, ...system..., sistema, micrologiciel) UND ein Verb bzw. "neueste"
// (update, upgrade, aktualisier, actualis/actualiz, aggiorn, mise, mettre, neueste, neuste, latest, newest). Fehlt eines, kann keines der Muster
// greifen -- die std::regex-Suche (mit icase und "(?:[^.\n]|\.[0-9]){0,80}") ist dann unnoetig. Sie kostete auf der Konsole 1,4 s bis 2,1 s beim Laden der
// Regeln und Fehlercodes (Messung 12. Nov. 2026). Die Treffer bleiben dieselben; die Vorpruefung ist nur schneller.
bool contains_any(const std::string& low, std::initializer_list<const char*> words) {
    for (const char* w : words)
        if (low.find(w) != std::string::npos) return true;
    return false;
}

bool may_match(const std::string& text) {
    std::string low = text;
    for (char& c : low)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return contains_any(low, {"firmware", "syst", "sistema", "micrologiciel"}) &&
           contains_any(low, {"update", "upgrade", "aktualisier", "actualis", "actualiz", "aggiorn", "mise", "mettre", "neueste", "neuste", "latest", "newest"});
}

}  // namespace

std::string find_system_update_advice(const std::string& text) {
    if (!may_match(text)) return {};
    for (const std::regex& re : patterns()) {
        std::smatch m;
        if (std::regex_search(text, m, re)) return m.str();
    }
    return {};
}

void require_no_system_update_advice(const json& root, const std::string& what) { walk(root, "", what); }

}  // namespace medic
