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

// Franzoesisch: "mise a jour", "mettre a jour", Befehle "mettez a jour", "mets a jour"; "a" auch mit Akzent (klein oder gross)
const std::string kFrUpdate = R"((?:mise\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour|mett(?:re|ez)\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour|mets\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour))";

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
            // Franzoesisch: "mise a jour / mettre a jour / mettez a jour / actualiser ... micrologiciel / firmware / logiciel systeme" (und umgekehrt)
            std::regex("(" + kFrUpdate + R"(|actualis\w*))" + R"((?:[^.\n]|\.[0-9]){0,80}(micrologiciel|firmware|logiciel\s+syst))", flags),
            std::regex(R"((micrologiciel|firmware|logiciel\s+syst)(?:[^.\n]|\.[0-9]){0,80})" + std::string("(") + kFrUpdate + R"(|actualis\w*))", flags),
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
            std::regex(kFrUpdate + R"(\s+du\s+syst(?:\xc3\xa8|e)me(?!\s+de\s+fichiers))", flags),
        };
    }();
    return p;
}

// Weitere Formulierungen (Review 13. Nov. 2026): Firmware/Systemsoftware INSTALLIEREN, "FW 12.00" statt "Firmware", die Konsole selbst aktualisieren.
// Bewusst eng gefasst (Verb und Objekt unmittelbar nebeneinander, das Objekt endet am Satzende oder vor einer Versionsangabe): "Installiere BackPork, damit
// das Spiel auf der Firmware laeuft", "Install the firmware backport plugin", "Update kstuff on FW 12.00", "Die installierte Firmware ist 12.00" und
// "Update the PS5 Medic rules" duerfen nicht treffen, sonst lehnt jede Konsole ein signiertes Regel-Update mit so einem Satz ab. Jede Gruppe hat eine
// eigene Vorpruefung (Gate), damit das Laden der Regeln auf der Konsole schnell bleibt.
struct Extra {
    int gate;  // 1 = installieren, 2 = FW/Firmware-Version, 3 = Konsole
    std::regex re;
};

const std::vector<Extra>& extra_patterns() {
    static const std::vector<Extra> p = [] {
        const auto flags = std::regex::ECMAScript | std::regex::icase;
        const std::string noun = R"((?:system\s*software|systemsoftware|firmware|\bfw\b)(?:[- ]version)?)";
        const std::string pre = R"((?:(?:ps5|playstation|offizielle\w*|official|sony)\s+)*)";
        const std::string adj = R"((?:neue\w*|new|newer|newest|latest|aktuelle\w*|current|h(?:\xc3\xb6|o)here\w*|higher)\s+)";
        const std::string art = R"((?:(?:a|an|the|die|das|eine|ein|diese|this|your|deine)\s+)?)";
        // Nur Grundform und Befehl ("installieren", "installiere", "install", "einspielen", "flash"), nicht "installierte Firmware" (Zustand)
        const std::string verb = R"((?:install(?:iere|ieren|ing)?|einspielen|flash(?:e|en|ing)?)\b)";
        const std::string end = R"((?=\s*(?:[.!,;:?\n)]|$)|\s+(?:v?[0-9]|version\b|(?:from|von)\s+sony)))";
        const std::string sentence_end = R"((?=\s*(?:[.!,;:?\n]|$)))";
        const std::string upd = R"((?:aktualisier\w*|updat\w*|upgrad\w*))";
        return std::vector<Extra>{
            // "Install a newer firmware", "Flash the latest FW", "Install the firmware.", "Neue Firmware installieren", "Die aktuelle Systemsoftware installieren"
            {1, std::regex(verb + R"(\s+)" + art + "(?:" + adj + ")?" + pre + noun + end, flags)},
            {1, std::regex(art + adj + pre + noun + R"(\s+(?:zu\s+)?(?:installieren|einspielen|flashen|install|flash)\b)", flags)},
            {1, std::regex(noun + R"(\s+(?:zu\s+)?(?:installieren|einspielen|flashen)\b)", flags)},
            // Franzoesisch, Italienisch, Spanisch: "Installez le micrologiciel", "Installa il firmware", "Instala el nuevo firmware"
            {1, std::regex(R"(install(?:ez|er)\s+(?:le|la|une|la\s+derni\S*)\s+(?:micrologiciel|firmware))", flags)},
            {1, std::regex(R"(install(?:a|are)\s+(?:il|un|il\s+nuovo)\s+firmware)", flags)},
            {1, std::regex(R"(instal(?:a|ar)\s+(?:el|un|el\s+nuevo)\s+firmware)", flags)},
            // "Auf FW 12.00 aktualisieren", "Update to FW 13.60", "FW aktualisieren", "Update to the latest FW", "Die FW auf 12.00 aktualisieren"
            {2, std::regex(R"((?:auf|to|zu)\s+(?:fw|firmware)\s*v?[0-9]+(?:\.[0-9]+)?\s+)" + upd, flags)},
            {2, std::regex(upd + R"(\s+(?:(?:auf|to|zu)\s+)?(?:(?:the|die|deine|your)\s+)?(?:(?:latest|neueste\w*|newest)\s+)?(?:fw|firmware)\s*v?[0-9])", flags)},
            {2, std::regex(upd + R"(\s+(?:(?:auf|to|zu)\s+)?(?:(?:the|die|deine|your)\s+)?(?:(?:latest|neueste\w*|newest)\s+)?\bfw\b)", flags)},
            {2, std::regex(R"(\bfw\b\s+(?:aktualisieren|aktualisiere|update|updaten|upgrade)\b)", flags)},
            {2, std::regex(R"(\bfw\b\s+(?:auf|to)\s+v?[0-9][0-9.]*\s+)" + upd, flags)},
            // "Update your PS5.", "Die Konsole aktualisieren." (nur wenn die Konsole das Objekt ist und der Satz dort endet), dazu fr/it/es
            {3, std::regex(R"((?:update|upgrade|aktualisier\w*)\s+(?:your|the|deine|deiner|die|eure?)\s+(?:ps5|playstation|console|konsole))" + sentence_end, flags)},
            {3, std::regex(R"((?:die|deine|the|your)\s+(?:ps5|playstation|konsole|console)\s+(?:aktualisieren|updaten|upgraden)\b)" + sentence_end, flags)},
            {3, std::regex(R"((?:mett(?:re|ez)|mets)\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour\s+(?:votre|ta|ton|la)\s+(?:console|ps5))", flags)},
            {3, std::regex(R"(aggiorn(?:a|are)\s+(?:la\s+tua|la|il)\s+(?:ps5|console))", flags)},
            {3, std::regex(R"(actualiz(?:a|ar)\s+(?:la|tu)\s+(?:consola|ps5))", flags)},
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

std::string lowered(const std::string& text) {
    std::string low = text;
    for (char& c : low)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return low;
}

bool may_match(const std::string& low) {
    return contains_any(low, {"firmware", "syst", "sistema", "micrologiciel"}) &&
           contains_any(low, {"update", "upgrade", "aktualisier", "actualis", "actualiz", "aggiorn", "mise", "mett", "mets", "neueste", "neuste", "latest", "newest"});
}

bool gate_open(int gate, const std::string& low) {
    switch (gate) {
        case 1: return contains_any(low, {"instal", "einspiel", "flash"}) && contains_any(low, {"firmware", "systemsoftware", "system software", "fw", "micrologiciel"});
        case 2: return contains_any(low, {"fw", "firmware"}) && contains_any(low, {"update", "upgrade", "aktualisier"});
        case 3:
            return contains_any(low, {"ps5", "playstation", "konsole", "consol"}) &&
                   contains_any(low, {"update", "upgrade", "aktualisier", "mett", "mets", "aggiorn", "actualiz"});
        default: return false;
    }
}

}  // namespace

std::string find_system_update_advice(const std::string& text) {
    const std::string low = lowered(text);
    if (may_match(low))
        for (const std::regex& re : patterns()) {
            std::smatch m;
            if (std::regex_search(text, m, re)) return m.str();
        }
    for (const Extra& e : extra_patterns()) {
        if (!gate_open(e.gate, low)) continue;
        std::smatch m;
        if (std::regex_search(text, m, e.re)) return m.str();
    }
    return {};
}

void require_no_system_update_advice(const json& root, const std::string& what) { walk(root, "", what); }

}  // namespace medic
