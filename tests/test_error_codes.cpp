// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des Fehlercode-Nachschlagewerks: Laden und Schema-Pruefung (libmedic/src/error_codes.cpp) und der Inhalt von
// knowledge/error_codes.json. Die Anzeige in der UI pruefen die Tests in test_ui.cpp.
#include "check.hpp"
#include "medic/error_codes.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

using medic::ErrorCode;
using medic::ErrorCodeSet;
using nlohmann::json;

namespace {

bool load_fails(const std::string& text) {
    try {
        medic::load_error_codes_from_string(text);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

// Ein gueltiger Eintrag als JSON-Objekt; die Tests aendern einzelne Felder und erwarten Ablehnung.
json good_entry() {
    return json::parse(R"({
      "id": "CE-100028-1",
      "title": {"de": "Titel", "en": "Title"},
      "text": {"de": "Erklaerung.", "en": "Explanation."},
      "steps": {"de": ["Schritt"], "en": ["Step"]},
      "source": "https://www.playstation.com/en-us/support/error-codes/ps5/ce-100028-1/",
      "level": "doc"
    })");
}

std::string file_with(const json& entry) {
    json root = {{"schema_version", 1}, {"db_version", "t"}, {"codes", json::array({entry})}};
    return root.dump();
}

bool entry_fails(const std::function<void(json&)>& mutate) {
    json e = good_entry();
    mutate(e);
    return load_fails(file_with(e));
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool mentions(const std::vector<std::string>& v, const std::string& needle) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return s.find(needle) != std::string::npos; });
}

void schema_tests() {
    const ErrorCodeSet ok = medic::load_error_codes_from_string(file_with(good_entry()));
    CHECK(ok.db_version == "t" && ok.codes.size() == 1, "Fehlercodes: gueltige Datei wird geladen");
    const ErrorCode& c = ok.codes[0];
    CHECK(c.id == "CE-100028-1" && c.title_de == "Titel" && c.title_en == "Title" && c.text_de == "Erklaerung." && c.text_en == "Explanation.",
          "Fehlercodes: Felder werden uebernommen");
    CHECK(c.steps_de.size() == 1 && c.steps_en.size() == 1 && c.notes_de.empty() && c.more_sources.empty() && c.level == "doc",
          "Fehlercodes: Schritte uebernommen, optionale Felder leer");
    CHECK(ok.find("CE-100028-1") == &ok.codes[0] && ok.find("CE-000000-0") == nullptr, "Fehlercodes: find() sucht nach der ID");

    {  // optionale Felder und Hinweisschluessel mit Unterstrich
        json e = good_entry();
        e["notes"] = {{"de", {"Hinweis"}}, {"en", {"Note"}}};
        e["more_sources"] = {"https://example.org/a"};
        json root = {{"schema_version", 1}, {"db_version", "t"}, {"_license", "SPDX"}, {"_note", "x"}, {"codes", json::array({e})}};
        const ErrorCodeSet s = medic::load_error_codes_from_string(root.dump());
        CHECK(s.codes[0].notes_de.size() == 1 && s.codes[0].notes_en[0] == "Note" && s.codes[0].more_sources.size() == 1,
              "Fehlercodes: notes und more_sources werden gelesen, _license/_note am Wurzelobjekt sind erlaubt");
    }
    {  // community darf ohne Schritte sein
        json e = good_entry();
        e["level"] = "community";
        e["steps"] = {{"de", json::array()}, {"en", json::array()}};
        CHECK(!load_fails(file_with(e)), "Fehlercodes: community ohne Schritte ist erlaubt");
    }

    CHECK(load_fails("{"), "Schema: kein JSON");
    CHECK(load_fails("[]"), "Schema: Wurzel ist kein Objekt");
    CHECK(load_fails(R"({"schema_version":2,"db_version":"t","codes":[]})"), "Schema: falsche schema_version");
    CHECK(load_fails(R"({"db_version":"t","codes":[]})"), "Schema: schema_version fehlt");
    CHECK(load_fails(R"({"schema_version":1,"codes":[]})"), "Schema: db_version fehlt");
    CHECK(load_fails(R"({"schema_version":1,"db_version":"t"})"), "Schema: codes fehlt");
    CHECK(load_fails(R"({"schema_version":1,"db_version":"t","codes":{}})"), "Schema: codes ist kein Array");
    CHECK(load_fails(R"({"schema_version":1,"db_version":"t","codes":[],"extra":1})"), "Schema: unbekannter Schluessel an der Wurzel");
    CHECK(!load_fails(R"({"schema_version":1,"db_version":"t","codes":[]})"), "Schema: leere Liste ist gueltig");
    CHECK(load_fails(R"({"schema_version":1,"db_version":"t","codes":[3]})"), "Schema: Eintrag ist kein Objekt");

    CHECK(entry_fails([](json& e) { e.erase("id"); }), "Schema: id fehlt");
    for (const char* bad : {"ce-100028-1", "CE-10002-1", "CE-100028-12", "CE100028-1", "XXX-100028-1", ""})
        CHECK(entry_fails([&](json& e) { e["id"] = bad; }), std::string("Schema: ungueltige ID '") + bad + "'");
    CHECK(entry_fails([](json& e) { e["title"].erase("en"); }), "Schema: Titel ohne Englisch");
    CHECK(entry_fails([](json& e) { e["title"]["de"] = ""; }), "Schema: leerer Titel");
    CHECK(entry_fails([](json& e) { e["title"]["fr"] = "x"; }), "Schema: unbekannte Sprache im Titel");
    CHECK(entry_fails([](json& e) { e.erase("text"); }), "Schema: Erklaerung fehlt");
    CHECK(entry_fails([](json& e) { e["text"]["en"] = 5; }), "Schema: Erklaerung ist kein String");
    CHECK(entry_fails([](json& e) { e.erase("steps"); }), "Schema: steps fehlt");
    CHECK(entry_fails([](json& e) { e["steps"]["en"] = json::array({"a", "b"}); }), "Schema: Schritte in de und en nicht gleich viele");
    CHECK(entry_fails([](json& e) { e["steps"]["de"] = json::array({""}); }), "Schema: leerer Schritt");
    CHECK(entry_fails([](json& e) { e["steps"]["de"] = json::array({3}); }), "Schema: Schritt ist kein String");
    CHECK(entry_fails([](json& e) { e["steps"]["de"] = "Schritt"; }), "Schema: Schritte sind kein Array");
    CHECK(entry_fails([](json& e) { e["steps"] = {{"de", json::array()}, {"en", json::array()}}; }), "Schema: doc ohne Schritte");
    CHECK(entry_fails([](json& e) { e["notes"] = {{"de", {"a"}}, {"en", json::array()}}; }), "Schema: Hinweise nicht gleich viele");
    CHECK(entry_fails([](json& e) { e["notes"] = {{"de", {""}}, {"en", {"x"}}}; }), "Schema: leerer Hinweis");
    CHECK(entry_fails([](json& e) { e.erase("source"); }), "Schema: Quelle fehlt");
    CHECK(entry_fails([](json& e) { e["source"] = "http://www.playstation.com/x"; }), "Schema: Quelle ohne https");
    CHECK(entry_fails([](json& e) { e["source"] = "https://x y"; }), "Schema: Quelle mit Leerzeichen");
    CHECK(entry_fails([](json& e) { e["source"] = "https://"; }), "Schema: Quelle ohne Adresse");
    CHECK(entry_fails([](json& e) { e["more_sources"] = "https://example.org"; }), "Schema: more_sources ist kein Array");
    CHECK(entry_fails([](json& e) { e["more_sources"] = {"ftp://example.org/x"}; }), "Schema: weitere Quelle ohne https");
    CHECK(entry_fails([](json& e) { e.erase("level"); }), "Schema: level fehlt");
    CHECK(entry_fails([](json& e) { e["level"] = "todo"; }), "Schema: level todo gibt es hier nicht");
    CHECK(entry_fails([](json& e) { e["fix"] = "irgendwas"; }), "Schema: unbekannter Schluessel im Eintrag (Tippfehler nicht stillschweigend schlucken)");

    {  // doppelte ID
        json root = {{"schema_version", 1}, {"db_version", "t"}, {"codes", json::array({good_entry(), good_entry()})}};
        CHECK(load_fails(root.dump()), "Schema: doppelte ID");
    }
    bool threw_runtime = false;
    try {
        medic::load_error_codes_from_file("/gibt/es/nicht.json");
    } catch (const std::runtime_error&) {
        threw_runtime = true;
    }
    CHECK(threw_runtime, "Fehlercodes: fehlende Datei -> runtime_error");
}

// Die ausgelieferte Datei: alle belegten Codes, Quelle je Eintrag, sortiert, sichere Hinweise vorhanden.
void content_tests() {
    const ErrorCodeSet set = medic::load_error_codes_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/error_codes.json");
    CHECK(!set.db_version.empty(), "error_codes.json: db_version gesetzt");

    const std::set<std::string> expected = {
        "CE-108262-9", "CE-107857-8", "CE-106485-4", "CE-108255-1",                                                   // System-/App-Daten, Update
        "CE-100028-1", "CE-107872-5", "CE-100006-7", "CE-109737-7",                                                   // Speicher, Laufwerke
        "NW-102265-6", "NW-102216-2", "CE-105799-1", "CE-118527-4", "CE-109245-1",                                    // Netz, Lizenz
        "NP-102944-0", "NP-102945-1", "CE-108889-4", "WS-116328-1", "CE-107520-5",                                    // neueste Systemsoftware
        "CE-105733-9"};                                                                                               // nur Community (CE-108264-1 bewusst nicht: keine Bedeutung belegt)
    std::set<std::string> have;
    for (const ErrorCode& c : set.codes) have.insert(c.id);
    CHECK(have == expected, "error_codes.json: genau die belegten Codes (" + std::to_string(set.codes.size()) + ")");
    CHECK(set.codes.size() == expected.size(), "error_codes.json: keine doppelten Codes");

    CHECK(std::is_sorted(set.codes.begin(), set.codes.end(), [](const ErrorCode& a, const ErrorCode& b) { return a.id < b.id; }),
          "error_codes.json: nach id sortiert (die Liste in der UI sucht man so)");

    const std::set<std::string> community = {"CE-105733-9"};
    for (const ErrorCode& c : set.codes) {
        const std::string tag = "error_codes.json " + c.id + ": ";
        CHECK(!c.source.empty(), tag + "Quelle vorhanden");
        CHECK(c.level == (community.count(c.id) ? "community" : "doc"), tag + "Stufe passt (nur die beiden Codes ohne Sony-Seite sind community)");
        if (c.level == "doc") {
            // Sony-Muster, am 9. Nov. 2026 abgerufen: .../ps5/<code in Kleinbuchstaben>/
            CHECK(c.source == "https://www.playstation.com/en-us/support/error-codes/ps5/" + lower(c.id) + "/", tag + "doc-Quelle ist die Sony-Seite des Codes");
            CHECK(!c.steps_de.empty() && c.steps_de.size() == c.steps_en.size(), tag + "Schritte in beiden Sprachen");
        } else {
            CHECK(c.source.find("playstation.com") == std::string::npos, tag + "community-Quelle ist keine Sony-Seite");
            CHECK(c.text_de.find("keine Sony-Seite") != std::string::npos && c.text_en.find("no Sony page") != std::string::npos,
                  tag + "Erklaerung sagt, dass es keine Sony-Seite gibt");
        }
        CHECK(c.text_de != c.text_en && c.title_de != c.title_en, tag + "Deutsch und Englisch sind verschiedene Texte");
        for (const auto& s : c.more_sources) CHECK(s.rfind("https://", 0) == 0, tag + "weitere Quelle mit https");
    }

    // Sicherheitsaussagen aus der Sony-Pruefung (Safe-Mode-Seite): Datenbank neu aufbauen nur mit "vorher sichern",
    // Zuruecksetzen loescht alles, und nichts davon geschieht automatisch (die Datei enthaelt nur Text, keine Aktion).
    for (const char* id : {"CE-108262-9", "CE-107857-8"}) {
        const ErrorCode* c = set.find(id);
        CHECK(c != nullptr, std::string("Safe Mode: ") + id + " vorhanden");
        if (!c) continue;
        CHECK(mentions(c->steps_de, "Rebuild Database") && mentions(c->steps_de, "vorher Daten sichern"), std::string("Safe Mode: ") + id + " Rebuild Database nur mit 'vorher sichern' (de)");
        CHECK(mentions(c->steps_en, "Rebuild Database") && mentions(c->steps_en, "back up"), std::string("Safe Mode: ") + id + " Rebuild Database nur mit 'back up' (en)");
        CHECK(mentions(c->notes_de, "Repair Console Storage") && mentions(c->notes_de, "Reset PS5"), std::string("Safe Mode: ") + id + " Optionen laut Sony erklaert (de)");
        CHECK(mentions(c->notes_en, "Repair Console Storage") && mentions(c->notes_en, "Reset PS5"), std::string("Safe Mode: ") + id + " Optionen laut Sony erklaert (en)");
        CHECK(std::find(c->more_sources.begin(), c->more_sources.end(), "https://www.playstation.com/en-us/support/hardware/safe-mode-playstation/") != c->more_sources.end(),
              std::string("Safe Mode: ") + id + " nennt die Safe-Mode-Seite als weitere Quelle");
    }
    if (const ErrorCode* c = set.find("CE-108262-9")) {
        CHECK(mentions(c->steps_de, "löscht alle Daten") && mentions(c->steps_de, "letzter Schritt"), "Safe Mode: Zuruecksetzen loescht alles und ist der letzte Schritt (de)");
        CHECK(mentions(c->steps_en, "erases all data") && mentions(c->steps_en, "last step"), "Safe Mode: Zuruecksetzen loescht alles und ist der letzte Schritt (en)");
    }
    // Sonys Platzangabe gilt nur fuer CE-107872-5; bei CE-100028-1 nennt Sony keine Menge.
    if (const ErrorCode* c = set.find("CE-107872-5")) CHECK(c->text_de.find("doppelt") != std::string::npos && c->text_en.find("twice") != std::string::npos, "CE-107872-5: doppelter freier Platz");
    if (const ErrorCode* c = set.find("CE-100028-1")) CHECK(c->text_de.find("keinen konkreten Platzbedarf") != std::string::npos, "CE-100028-1: keine Platzmenge erfunden");
    // Spiele auf einer gejailbreakten Konsole: kein Systemupdate als Loesung. Wo Sony nur das Update nennt, steht stattdessen der Hinweis
    // auf einen Backport fuer die genutzte Firmware oder AMPR Emu / AMPR Emu NG (Marc, 9. Nov.); bei den Codes mit mehreren Sony-Schritten
    // entfaellt nur der Update-Schritt.
    auto recommends_update = [](const ErrorCode& c) {
        return mentions(c.steps_de, "Systemsoftware aktualisieren") || mentions(c.steps_de, "Systemsoftware und") || mentions(c.steps_de, "Systemupdate") ||
               mentions(c.steps_en, "pdate the system software") || mentions(c.steps_en, "system update");
    };
    for (const char* id : {"NP-102944-0", "NP-102945-1", "CE-108889-4", "WS-116328-1", "CE-107520-5", "CE-106485-4"}) {
        const ErrorCode* c = set.find(id);
        CHECK(c != nullptr, std::string("Spiele: ") + id + " vorhanden");
        if (!c) continue;
        const std::string tag = std::string("Spiele: ") + id + ": ";
        CHECK(!recommends_update(*c), tag + "kein Systemupdate als Schritt");
        CHECK(mentions(c->steps_de, "Backport") && mentions(c->steps_en, "backport"), tag + "Schritt nennt den Backport fuer die genutzte Firmware");
        CHECK(mentions(c->steps_de, "AMPR Emu NG") && mentions(c->steps_en, "AMPR Emu NG") && mentions(c->steps_de, "AMPR Emu oder") && mentions(c->steps_en, "AMPR Emu or"),
              tag + "Schritt nennt AMPR Emu und AMPR Emu NG");
        CHECK(mentions(c->steps_de, "libSceAmpr.sprx") && mentions(c->steps_de, "fakelib/") && mentions(c->steps_de, "ShadowMount+"), tag + "Einrichtung laut README von AMPR Emu NG");
        CHECK(mentions(c->notes_de, "Szene (nicht von Sony)") && mentions(c->notes_en, "scene (not from Sony)"), tag + "Szenewissen ist als nicht von Sony gekennzeichnet");
        CHECK(mentions(c->notes_de, "nicht belegt") && mentions(c->notes_en, "not documented"), tag + "Hinweis: die Zuordnung des Codes zu diesem Fall ist nicht belegt");
        CHECK(mentions(c->notes_de, "Systemupdate wird hier nicht empfohlen") && mentions(c->notes_de, "stellt weder Spiele noch Backports bereit"), tag + "Medic empfiehlt kein Update und liefert keine Spiele/Backports (de)");
        CHECK(mentions(c->notes_en, "system update is not recommended") && mentions(c->notes_en, "neither games nor backports"), tag + "Medic empfiehlt kein Update und liefert keine Spiele/Backports (en)");
        CHECK(std::find(c->more_sources.begin(), c->more_sources.end(), "https://github.com/quer3q/ampr-emu-ng") != c->more_sources.end(), tag + "AMPR-Emu-NG-Quelle genannt");
    }
    for (const char* id : {"CE-108262-9", "CE-107857-8"}) {
        const ErrorCode* c = set.find(id);
        if (!c) continue;
        CHECK(!recommends_update(*c), std::string("Spiele: ") + id + ": Sonys Systemupdate-Schritt entfaellt");
        CHECK(mentions(c->notes_de, "fehlt hier mit Absicht") && mentions(c->notes_en, "left out on purpose"), std::string("Spiele: ") + id + ": Hinweis, warum der Schritt fehlt");
    }
    // Wo noch ein Systemupdate steht (Laufwerk-Fehler, kein Spiel), bleibt der Hinweis auf die Folge fuer den Jailbreak.
    for (const ErrorCode& c : set.codes)
        if (recommends_update(c)) CHECK(mentions(c.notes_de, "Jailbreak") && mentions(c.notes_en, "jailbreak"), "Systemupdate: Jailbreak-Hinweis bei " + c.id);
    if (const ErrorCode* c = set.find("CE-100006-7")) CHECK(recommends_update(*c), "CE-100006-7: Laufwerk-Fehler behaelt Sonys Update-Schritt (kein Spiel)");
}

}  // namespace

void error_code_tests() {
    schema_tests();
    content_tests();
}
