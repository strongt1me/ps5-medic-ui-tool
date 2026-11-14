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

// Verb (de/en) mit allen Formen ("updating", "aktualisiert").
const std::string kUpdVerb = R"((?:aktualisier\w*|updat\w*|upgrad\w*)\b)";

// Ausnahme fuer die Muster "Verb ... Firmware" und "Firmware ... Verb": Gilt das Verb einem genannten Bestandteil ("Update kstuff-lite to a version that supports
// firmware 13.60.", "Wenn die Firmware 13.60 ist, aktualisiere kstuff-lite."), ist es kein Systemupdate-Hinweis. Folgt dem Bestandteil aber noch etwas
// ("Update kstuff and the firmware.", "Aktualisiere kstuff, ShadowMount+ und die Systemsoftware."), bleibt der Treffer.
const std::regex& component_after_verb() {
    static const std::regex r(R"(^\s+(?:(?:the|die|das|den|dein\w*|your)\s+)?(?:kstuff|etahen|shadowmount\w*|backpork|payloads?|plugins?|ps5\s*medic|medic|regeln|rules|apps?|games?|spiele?)\b)",
                              std::regex::ECMAScript | std::regex::icase);
    return r;
}

const std::regex& conjunction() {
    static const std::regex r(R"(\b(?:and|und|then|danach|dann|plus|sowie|also|auch|or|oder)\b|[,;&+])", std::regex::ECMAScript | std::regex::icase);
    return r;
}

// Alles ohne Gross-/Kleinschreibung. "(?:[^.\n]|\.[0-9]){0,80}": derselbe Satz, damit der Menuepfad "System Software > Reset Options ..." und
// ein spaeterer, getrennter Satz ueber ein Spiel-Update nicht zusammenfallen.
const std::vector<std::regex>& patterns() {
    static const std::vector<std::regex> p = [] {
        const auto flags = std::regex::ECMAScript | std::regex::icase;
        return std::vector<std::regex>{
            std::regex(R"(system\s*-?\s*(update|upgrade))", flags),                                            // Systemupdate, System-Update, system update
            std::regex(R"(firmware\s*-?\s*(update|upgrade|aktualisier))", flags),                              // Firmware-Update, Firmware aktualisieren
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
        // Hinter "die Konsole": Satzende, "auf/to <die neueste> Version/Firmware/System ..." oder "software"/"system ..." ("Update your console software.")
        const std::string console_end = R"((?=\s*(?:[.!,;:?\n]|$)|\s+(?:to|auf|on|zu)\s+(?:the\s+|die\s+|deine\s+|your\s+|eine\s+|a\s+)?(?:(?:latest|newest|neueste\w*|neue\w*|new|newer|current|aktuelle\w*)\s+)?(?:version|firmware|fw|system\w*)\b|\s+(?:software|system\w*)\b))";
        return std::vector<Extra>{
            // "Install a newer firmware", "Flash the latest FW", "Install the firmware.", "Neue Firmware installieren", "Die aktuelle Systemsoftware installieren"
            {1, std::regex(verb + R"(\s+)" + art + "(?:" + adj + ")?" + pre + noun + end, flags)},
            {1, std::regex(art + adj + pre + noun + R"(\s+(?:neu\s+)?(?:zu\s+)?(?:installieren|einspielen|aufspielen|aufzuspielen|flashen|install|flash)\b)", flags)},
            {1, std::regex(noun + R"(\s+(?:neu\s+)?(?:zu\s+)?(?:installieren|einspielen|aufspielen|aufzuspielen|flashen)\b)", flags)},
            {1, std::regex(R"(\bspiel(?:e|en)\s+)" + art + "(?:" + adj + ")?" + pre + noun + R"(\s+(?:neu\s+)?ein\b)", flags)},  // "Spiele eine neuere Firmware ein."
            // Franzoesisch, Italienisch, Spanisch: "Installez le micrologiciel", "Installez le dernier micrologiciel", "Installa l'ultimo firmware", "Instala el nuevo firmware"
            {1, std::regex(R"(install(?:ez|er)\s+(?:(?:le|la|l['\xe2]|une?|votre|cette)\S*\s+)?(?:(?:derni\S*|nouvel\S*|nouveau|r\S*cent\S*)\s+)?(?:micrologiciel|firmware))", flags)},
            {1, std::regex(R"(install(?:a|are)\s+(?:(?:il|un|l['\xe2]|il\s+tuo)\S*\s*)?(?:(?:nuovo|ultimo|pi\S*\s+recente|pi\S*\s+nuovo)\s+)?firmware)", flags)},
            {1, std::regex(R"(instal(?:a|ar)\s+(?:(?:el|un|tu)\s+)?(?:(?:nuevo|\xc3\xbaltimo|ultimo|m\S*s\s+reciente)\s+)?firmware)", flags)},
            // "Auf FW 12.00 aktualisieren", "Update to FW 13.60", "FW aktualisieren", "Update to the latest FW", "Die FW auf 12.00 aktualisieren"
            {2, std::regex(R"((?:auf|to|zu)\s+(?:fw|firmware)\s*v?[0-9]+(?:\.[0-9]+)?\s+)" + upd, flags)},
            {2, std::regex(upd + R"(\s+(?:(?:auf|to|zu)\s+)?(?:(?:the|die|deine|your)\s+)?(?:(?:latest|neueste\w*|newest)\s+)?(?:fw|firmware)\s*v?[0-9])", flags)},
            {2, std::regex(upd + R"(\s+(?:(?:auf|to|zu)\s+)?(?:(?:the|die|deine|your)\s+)?(?:(?:latest|neueste\w*|newest)\s+)?\bfw\b)", flags)},
            {2, std::regex(R"(\bfw\b\s+(?:aktualisieren|aktualisiere|update|updaten|upgrade)\b)", flags)},
            {2, std::regex(R"(\bfw\b\s+(?:auf|to)\s+v?[0-9][0-9.]*\s+)" + upd, flags)},
            // "Update your PS5.", "Die Konsole aktualisieren." (nur wenn die Konsole das Objekt ist und der Satz dort endet), dazu fr/it/es
            {3, std::regex(upd + R"(\s+(?:your|the|deine|deiner|die|eure?)\s+(?:ps5|playstation|console|konsole))" + console_end, flags)},
            {3, std::regex(R"((?:die|deine|the|your)\s+(?:ps5|playstation|konsole|console)\s+(?:aktualisieren|updaten|upgraden|updating|upgrading)\b)" + sentence_end, flags)},
            {3, std::regex(R"((?:die|deine|the|your)\s+(?:ps5|playstation|konsole|console)\s+(?:auf|to)\s+[^.\n]{0,40}\b(?:version|firmware|fw)\b[^.\n]{0,20}\b)" + upd, flags)},
            {3, std::regex(R"((?:mett(?:re|ez)|mets)\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour\s+(?:votre|ta|ton|la)\s+(?:console|ps5))", flags)},
            {3, std::regex(R"((?:mett(?:re|ez)|mets)\s+(?:votre|ta|ton|la)\s+(?:console|ps5)\s+(?:\xc3\xa0|\xc3\x80|a)\s+jour)", flags)},
            {3, std::regex(kFrUpdate + R"(\s+(?:du\s+|le\s+)?logiciel\s+de\s+(?:la|votre)\s+console)", flags)},
            {3, std::regex(R"(aggiorn(?:a|are)\s+(?:la\s+tua|la|il|il\s+tuo)\s+(?:ps5|console|sistema(?!\s+di\s+file)))", flags)},
            {3, std::regex(R"(aggiorn(?:a|are)\s+il\s+software\s+(?:della|di)\s+(?:console|sistema))", flags)},
            {3, std::regex(R"(actualiz(?:a|ar)\s+(?:la|tu|el)\s+(?:consola|ps5|sistema(?!\s+de\s+archivos)))", flags)},
            {3, std::regex(R"(actualiz(?:a|ar)\s+el\s+software\s+de\s+(?:la\s+)?(?:consola|sistema))", flags)},
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

// Typografische Varianten, mit denen sich die Muster umgehen liessen (auch beim Kopieren von der Sony-Seite): geschuetzte/schmale/Duenn-Leerzeichen (UTF-8,
// von \s nicht erfasst) -> ' ', Bindestrich-Varianten (U+2010 bis U+2015, Minus) -> '-', unsichtbare Zeichen und kombinierende Akzente (zerlegtes "a" + Gravis) entfallen.
std::string normalized(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const unsigned char c1 = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0, c2 = i + 2 < s.size() ? static_cast<unsigned char>(s[i + 2]) : 0;
        if (c == 0xC2 && c1 == 0xA0) { out += ' '; i += 2; continue; }                                  // geschuetztes Leerzeichen
        if (c == 0xC2 && c1 == 0xAD) { i += 2; continue; }                                              // weicher Bindestrich
        if (c == 0xCC && c1 >= 0x80 && c1 <= 0xBF) { i += 2; continue; }                                // kombinierende Zeichen U+0300 bis U+033F
        if (c == 0xCD && c1 >= 0x80 && c1 <= 0xAF) { i += 2; continue; }                                // kombinierende Zeichen U+0340 bis U+036F
        if (c == 0xE2 && c1 == 0x80 && ((c2 >= 0x80 && c2 <= 0x8A) || c2 == 0xAF)) { out += ' '; i += 3; continue; }  // U+2000 bis U+200A, U+202F
        if (c == 0xE2 && c1 == 0x80 && c2 >= 0x8B && c2 <= 0x8D) { i += 3; continue; }                  // Nullbreiten-Zeichen
        if (c == 0xE2 && c1 == 0x80 && c2 >= 0x90 && c2 <= 0x95) { out += '-'; i += 3; continue; }      // Bindestrich-Varianten bis Geviertstrich
        if (c == 0xE2 && c1 == 0x81 && c2 == 0x9F) { out += ' '; i += 3; continue; }                    // mittleres mathematisches Leerzeichen
        if (c == 0xE2 && c1 == 0x88 && c2 == 0x92) { out += '-'; i += 3; continue; }                    // Minuszeichen
        if (c == 0xE3 && c1 == 0x80 && c2 == 0x80) { out += ' '; i += 3; continue; }                    // ideographisches Leerzeichen
        if (c == 0xEF && c1 == 0xBB && c2 == 0xBF) { i += 3; continue; }                                // Byte-Order-Mark
        out += s[i++];
    }
    return out;
}

// true, wenn normalized() etwas aendern koennte: Es reagiert nur auf diese Anfangsbytes (C2, CC, CD, E2, E3, EF); fuer alle anderen Texte ist das Ergebnis gleich dem Eingang.
bool needs_normalizing(const std::string& s) {
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == 0xC2 || c == 0xCC || c == 0xCD || c == 0xE2 || c == 0xE3 || c == 0xEF) return true;
    }
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
           contains_any(low, {"updat", "upgrad", "aktualisier", "actualis", "actualiz", "aggiorn", "mise", "mett", "mets", "neueste", "neuste", "latest", "newest"});
}

bool gate_open(int gate, const std::string& low) {
    switch (gate) {
        case 1: return contains_any(low, {"instal", "einspiel", "aufspiel", "aufzuspiel", "spiel", "flash"}) && contains_any(low, {"firmware", "systemsoftware", "system software", "fw", "micrologiciel"});
        case 2: return contains_any(low, {"fw", "firmware"}) && contains_any(low, {"updat", "upgrad", "aktualisier"});
        case 3:
            return contains_any(low, {"ps5", "playstation", "konsole", "consol", "sistem"}) &&
                   contains_any(low, {"updat", "upgrad", "aktualisier", "mise", "mett", "mets", "aggiorn", "actualiz"});
        default: return false;
    }
}

// "Verb ... Systemsoftware/Firmware" (verb_first) und "Systemsoftware/Firmware ... Verb", mit der Ausnahme fuer einen genannten Bestandteil (siehe oben)
std::string verb_noun_advice(const std::string& text, bool verb_first) {
    static const std::string kNoun = R"((?:system\s*software|systemsoftware|firmware))";
    static const std::regex first(kUpdVerb + R"((?:[^.\n]|\.[0-9]){0,80})" + kNoun, std::regex::ECMAScript | std::regex::icase);   // update the system software, Updating the firmware
    static const std::regex second(kNoun + R"((?:[^.\n]|\.[0-9]){0,80})" + kUpdVerb, std::regex::ECMAScript | std::regex::icase);  // Systemsoftware ... aktualisieren, Firmware needs updating
    const std::regex& re = verb_first ? first : second;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) {
        const std::string s = it->str();
        if (verb_first) {
            const auto sp = s.find_first_of(" \t");
            const std::string rest = sp == std::string::npos ? std::string() : s.substr(sp);
            if (std::regex_search(rest, component_after_verb()) && !std::regex_search(rest, conjunction())) continue;
        } else {
            const std::string after = text.substr(static_cast<std::size_t>(it->position() + it->length()));
            if (std::regex_search(after, component_after_verb())) continue;
        }
        return s;
    }
    return {};
}

}  // namespace

std::string find_system_update_advice(const std::string& raw) {
    // normalized() aendert nur Texte mit einem der Anfangsbytes unten; sonst ist die Kopie unnoetig (die Pruefung laeuft beim Laden ueber ~3700 Texte)
    const bool changed = needs_normalizing(raw);
    const std::string norm = changed ? normalized(raw) : std::string();
    const std::string& text = changed ? norm : raw;
    const std::string low = lowered(text);
    // Jedes Muster braucht mindestens eines dieser Woerter; die drei Tore (Extra-Muster) und may_match werden je Text einmal ausgewertet, nicht je Muster
    const bool maybe = may_match(low);
    bool gate[4] = {false, false, false, false};
    bool any_gate = false;
    for (int g = 1; g <= 3; ++g) any_gate = (gate[g] = gate_open(g, low)) || any_gate;
    if (!maybe && !any_gate) return {};
    if (maybe) {
        for (const std::regex& re : patterns()) {
            std::smatch m;
            if (std::regex_search(text, m, re)) return m.str();
        }
        for (const bool verb_first : {false, true}) {
            const std::string hit = verb_noun_advice(text, verb_first);
            if (!hit.empty()) return hit;
        }
    }
    if (!any_gate) return {};
    for (const Extra& e : extra_patterns()) {
        if (e.gate < 1 || e.gate > 3 || !gate[e.gate]) continue;
        std::smatch m;
        if (std::regex_search(text, m, e.re)) return m.str();
    }
    return {};
}

void require_no_system_update_advice(const json& root, const std::string& what) { walk(root, "", what); }

}  // namespace medic
