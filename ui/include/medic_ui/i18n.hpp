// SPDX-License-Identifier: GPL-3.0-or-later
// Texte (Deutsch, Englisch, Franzoesisch, Italienisch, Spanisch) aus ui/i18n/*.json. Die JSON-Dateien sind eingebettet (tools/embed_i18n.py).
// Die Wissenstexte (Regeln, Fehlercodes, Selbsttests) tragen nur Deutsch und Englisch; fr/it/es liegen als Ueberlagerung in knowledge/l10n/
// (Knowledge) und fallen auf Englisch zurueck, wo ein Eintrag fehlt.
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace medic::ui {

enum class Lang { De, En, Fr, It, Es };
constexpr Lang kAllLangs[] = {Lang::De, Lang::En, Lang::Fr, Lang::It, Lang::Es};

const char* lang_code(Lang l);                // "de", "en", "fr", "it", "es"
bool lang_from_code(std::string_view code, Lang& out);  // false bei unbekanntem Kuerzel

extern const char* const kDeJson;
extern const char* const kEnJson;
extern const char* const kFrJson;
extern const char* const kItJson;
extern const char* const kEsJson;
extern const char* const kFrKnowledge;
extern const char* const kItKnowledge;
extern const char* const kEsKnowledge;

class Strings {
public:
    static const Strings& get(Lang l);
    // Unbekannter Schluessel -> "!schluessel!" (sichtbar) und Eintrag in missing().
    std::string operator()(std::string_view key) const;
    // Ersetzt {name} durch den Wert.
    std::string fmt(std::string_view key, std::initializer_list<std::pair<std::string, std::string>> args) const;
    bool has(std::string_view key) const;
    std::vector<std::string> keys() const;

    static std::vector<std::string>& missing();  // fuer Tests

private:
    explicit Strings(const char* json_text);
    std::map<std::string, std::string, std::less<>> map_;
};

// Uebersetzte Wissenstexte fuer fr/it/es (knowledge/l10n/*.json). Fuer de und en liefert get() eine leere Ueberlagerung: dort stehen die
// Texte in den Regeln selbst. Alle Zugriffe geben nullptr, wenn der Eintrag fehlt (dann Englisch zeigen).
class Knowledge {
public:
    static const Knowledge& get(Lang l);
    // section: "rules", "coverage", "metrics", "selftests", "error_codes"; field z. B. "text", "confidence_reason", "name", "title"
    const std::string* text(std::string_view section, std::string_view id, std::string_view field) const;
    const std::vector<std::string>* list(std::string_view section, std::string_view id, std::string_view field) const;

private:
    explicit Knowledge(const char* json_text);
    std::map<std::string, std::string, std::less<>> texts_;                // "section\x1f" "id\x1f" "field"
    std::map<std::string, std::vector<std::string>, std::less<>> lists_;
};

}  // namespace medic::ui
