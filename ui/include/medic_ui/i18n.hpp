// SPDX-License-Identifier: GPL-3.0-or-later
// Texte (Deutsch/Englisch) aus ui/i18n/*.json. Die JSON-Dateien sind eingebettet (tools/embed_i18n.py).
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace medic::ui {

enum class Lang { De, En };

extern const char* const kDeJson;
extern const char* const kEnJson;

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

}  // namespace medic::ui
