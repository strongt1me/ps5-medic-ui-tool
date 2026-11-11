// SPDX-License-Identifier: GPL-3.0-or-later
// Textregel: PS5 Medic nennt nie ein Update der Systemsoftware/Firmware (Entscheidung von Marc, 10. Nov. 2026).
// Geprueft werden die Erkennung selbst, die Lader (auch fuer signierte Updates) und alle Texte, die die App ausliefert.
#include "check.hpp"
#include "medic/error_codes.hpp"
#include "medic/rules.hpp"
#include "medic/text_policy.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

using nlohmann::json;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool throws_policy(const json& j) {
    try {
        medic::require_no_system_update_advice(j, "test");
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

// Alle Strings eines JSON-Baums (ohne '_'-Felder) auf die Textregel pruefen; liefert die Stellen mit Treffern.
std::string offending(const json& j, const std::string& path = "") {
    if (j.is_string()) {
        const std::string hit = medic::find_system_update_advice(j.get<std::string>());
        return hit.empty() ? std::string() : path + ": " + hit + "\n";
    }
    std::string out;
    if (j.is_array())
        for (std::size_t i = 0; i < j.size(); ++i) out += offending(j[i], path + "[" + std::to_string(i) + "]");
    else if (j.is_object())
        for (auto it = j.begin(); it != j.end(); ++it)
            if (it.key().empty() || it.key()[0] != '_') out += offending(it.value(), path + "." + it.key());
    return out;
}

}  // namespace

void text_policy_tests() {
    using medic::find_system_update_advice;

    // Verboten: jede Form von "Systemsoftware/Firmware aktualisieren"
    for (const char* s : {"Die Systemsoftware auf die neueste Version aktualisieren.", "Ein Systemupdate ist hier nicht der Weg.", "Install a system update.",
                          "Update the system software to the latest version.", "Die Firmware des Routers aktualisieren.", "Firmware-Update einspielen",
                          "Auf eine neuere Firmware updaten: firmware upgrade", "Die neueste Systemsoftware installieren", "System-Update im Safe Mode",
                          "Sony empfiehlt Updates für Systemsoftware und App."})
        CHECK(!find_system_update_advice(s).empty(), std::string("Textregel erkennt: ") + s);

    // Erlaubt: andere Updates, der Menuepfad zum Zuruecksetzen, Firmware als reine Angabe
    for (const char* s : {"Das Spiel aktualisieren.", "ShadowMount+ aktualisieren.", "ps5-theme auf Version 1.53 oder neuer aktualisieren.",
                          "Hilft nichts davon, die Konsole zurücksetzen („Settings > System > System Software > Reset Options > Reset Your Console“). Danach das Spiel neu installieren.",
                          "Das Spiel ist für eine höhere Firmware-Version gebaut als die der Konsole. Es braucht einen Backport.", "Firmware: 12.00",
                          "Fakelib-Updates ausschalten", "Update ps5-theme to version 1.53 or newer."})
        CHECK(find_system_update_advice(s).empty(), std::string("Textregel erlaubt: ") + s + " -> " + find_system_update_advice(s));

    // Baumpruefung: '_'-Felder (Kommentare) und https-Adressen sind ausgenommen
    CHECK(throws_policy(json::parse(R"({"a":{"b":["ok","Systemsoftware aktualisieren"]}})")), "Baum: Treffer in verschachteltem Array");
    CHECK(!throws_policy(json::parse(R"({"_note":"Systemupdate","a":"https://example.org/system-update"})")), "Baum: _-Felder und Adressen ausgenommen");

    // Lader: eine Datei mit so einem Text wird abgelehnt (gilt damit auch fuer heruntergeladene, signierte Dateien)
    const std::string bad_rules = R"({"schema_version":1,"db_version":"t","rules":[{"id":"t.rule","severity":"info","when":{"fact":"x","eq":"1"},)"
                                  R"("text":{"de":"Bitte die Systemsoftware aktualisieren.","en":"e"},"source":"https://example.org","verified":"doc"}]})";
    bool rejected = false;
    try {
        medic::load_rules_from_string(bad_rules);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected, "rules.json mit Systemupdate-Hinweis wird abgelehnt");
    const std::string bad_codes = R"({"schema_version":1,"db_version":"t","codes":[{"id":"CE-100028-1","title":{"de":"T","en":"T"},)"
                                  R"("text":{"de":"Erklaerung.","en":"Explanation."},"steps":{"de":["Update the system software."],"en":["Update the system software."]},)"
                                  R"("source":"https://www.playstation.com/x","checked":"2026-11-09","level":"doc"}]})";
    rejected = false;
    try {
        medic::load_error_codes_from_string(bad_codes);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected, "error_codes.json mit Systemupdate-Hinweis wird abgelehnt");

    // Was die App ausliefert: Wissensdateien und UI-Texte duerfen nichts nennen
    const std::string root = MEDIC_SOURCE_DIR;
    for (const char* f : {"/knowledge/rules.json", "/knowledge/error_codes.json", "/ui/i18n/de.json", "/ui/i18n/en.json"}) {
        const json j = json::parse(slurp(root + f), nullptr, false);
        CHECK(!j.is_discarded(), std::string("lesbar: ") + f);
        if (j.is_discarded()) continue;
        const std::string bad = offending(j);
        CHECK(bad.empty(), std::string("kein Systemupdate-Hinweis in ") + f + ":\n" + bad);
    }
}
