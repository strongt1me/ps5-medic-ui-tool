// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des Fehlercode-Nachschlagewerks: Laden und Schema-Pruefung (libmedic/src/error_codes.cpp) und der Inhalt von
// knowledge/error_codes.json. Die Anzeige in der UI pruefen die Tests in test_ui.cpp.
#include "check.hpp"
#include "medic/error_codes.hpp"
#include "medic/newest.hpp"
#include "medic/text_policy.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
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
      "checked": "2026-11-09",
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

    // Hex-Fehler, Pruefdatum, Fingerabdruck, Kontext
    CHECK(!entry_fails([](json& e) { e["id"] = "0x809F8032"; e["level"] = "community"; e["steps"] = {{"de", json::array()}, {"en", json::array()}}; e["source"] = "https://example.org/x"; }),
          "Schema: Hex-Fehler 0x809F8032 ist eine gueltige ID");
    for (const char* bad : {"0x809f8032", "0x1234", "0x809F80320", "0X809F8032", "809F8032"})
        CHECK(entry_fails([&](json& e) { e["id"] = bad; }), std::string("Schema: ungueltige Hex-ID '") + bad + "'");
    CHECK(entry_fails([](json& e) { e.erase("checked"); }), "Schema: checked fehlt");
    for (const char* bad : {"09.11.2026", "2026-13-01", "2026-11-32", "2026-11-9", "heute"})
        CHECK(entry_fails([&](json& e) { e["checked"] = bad; }), std::string("Schema: ungueltiges Pruefdatum '") + bad + "'");
    CHECK(!entry_fails([](json& e) { e["source_sha256"] = std::string(64, 'a'); }), "Schema: gueltiger Fingerabdruck");
    for (const std::string bad : {std::string(63, 'a'), std::string(64, 'A'), std::string(64, 'g'), std::string()})
        CHECK(entry_fails([&](json& e) { e["source_sha256"] = bad; }), "Schema: ungueltiger Fingerabdruck '" + bad + "'");
    CHECK(!entry_fails([](json& e) { e["context"] = {"storage", "fw"}; }), "Schema: bekannte Kontextangaben");
    CHECK(medic::load_error_codes_from_string(file_with([] { json e = good_entry(); e["context"] = {"smp", "need_backport", "drives"}; return e; }())).codes[0].context.size() == 3,
          "Schema: context wird gelesen");
    CHECK(entry_fails([](json& e) { e["context"] = {"network"}; }), "Schema: unbekannte Kontextangabe (Tippfehler nicht schlucken)");
    CHECK(entry_fails([](json& e) { e["context"] = "storage"; }), "Schema: context ist ein Array");
    CHECK(entry_fails([](json& e) { e["context"] = {3}; }), "Schema: context enthaelt Strings");

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
        "CE-100005-6", "CE-100009-0", "CE-100095-5", "CE-100096-6", "CE-106487-6", "CE-107527-2", "CE-107622-8",      // Spiele und Apps: laden, starten,
        "CE-107750-0", "CE-107891-6", "CE-109573-5", "CE-117740-0", "NP-104291-7",                                    // installieren, Fehler beim Spielen, Lizenz
        "CE-105733-9", "CE-107098-5", "0x80268001", "0x80551618", "0x80960005", "0x809F8032",                        // nur Community (keine Sony-Seite)
        "CE-100008-9", "NW-102261-2", "WV-109145-0", "WV-109168-5",                                                 // aus dem Fehlerverlauf der Konsole (11. Nov. 2026), Sony-Seiten
        // 13. Nov. 2026: Sony-Seiten und PS5 Dev Wiki (ohne Codes, die nur ein Update der Systemsoftware nennen, und ohne E2-Codes)
        "CE-100002-3", "CE-100022-5", "CE-105638-3", "CE-105771-1", "CE-105841-9", "CE-106439-3",
        "CE-106444-9", "CE-106449-4", "CE-107031-2", "CE-107255-0", "CE-107649-7", "CE-107863-5",
        "CE-107928-7", "CE-107938-8", "CE-107958-0", "CE-108360-8", "CE-108862-5", "CE-108877-1",
        "CE-109258-5", "CE-109503-8", "CE-109506-1", "CE-109531-9", "CE-110032-7", "CE-110538-8",
        "CE-110551-3", "CE-110552-4", "CE-110555-7", "CE-111161-1", "CE-112069-9", "CE-112839-4",
        "CE-112840-6", "CE-112841-7", "CE-112987-8", "CE-112988-9", "CE-113212-0", "CE-113227-6",
        "CE-113511-2", "CE-113524-6", "CE-113661-8", "CE-116799-3", "CE-117625-2", "CE-117722-0",
        "CE-117773-6", "CE-118415-0", "CE-118851-4", "CE-119224-9", "CE-120706-6", "CE-122154-5",
        "NP-102937-2", "NP-102942-8", "NP-102943-9", "NP-102946-2", "NP-102947-3", "NP-102955-2",
        "NP-103019-4", "NP-103028-4", "NP-103047-5", "NP-103105-0", "NP-103107-2", "NP-103109-4",
        "NP-103111-7", "NP-103117-3", "NP-103148-7", "NP-103172-4", "NP-103539-1",
        "NP-104303-1", "NP-104433-5", "NP-104627-0", "NW-102254-4", "NW-102307-3", "NW-102308-4",
        "NW-102311-8", "NW-102314-1", "NW-102315-2", "NW-102379-2", "NW-102417-5", "NW-102633-5", "NW-102636-8", "NW-102650-4", "NW-102655-9", "WS-113947-5", "WS-114241-3",
        "WS-114298-5", "WS-115019-7", "WS-115195-2", "WS-115338-1", "WS-116327-0", "WS-116329-2",
        "WS-116330-4", "WS-116331-5", "WS-116332-6", "WS-116334-8", "WS-116367-4", "WS-116414-7",
        "WS-116417-0", "WS-116420-4", "WS-116439-4", "WS-116449-5", "WS-116483-3", "WS-116486-6",
        "WS-116489-9", "WS-116520-5", "WS-116521-6", "WS-117176-3", "WS-117224-7",
        "WS-119699-5", "WV-109144-9", "WV-109146-1", "WV-109153-9", "WV-109166-3"};
    std::set<std::string> have;
    for (const ErrorCode& c : set.codes) have.insert(c.id);
    CHECK(have == expected, "error_codes.json: genau die belegten Codes (" + std::to_string(set.codes.size()) + ")");
    CHECK(set.codes.size() == expected.size(), "error_codes.json: keine doppelten Codes");

    CHECK(std::is_sorted(set.codes.begin(), set.codes.end(), [](const ErrorCode& a, const ErrorCode& b) { return a.id < b.id; }),
          "error_codes.json: nach id sortiert (die Liste in der UI sucht man so)");

    const std::set<std::string> community = {"CE-105733-9", "CE-107098-5", "0x80268001", "0x80551618", "0x80960005", "0x809F8032",
                                           "CE-107031-2", "CE-107958-0", "CE-113661-8", "CE-117625-2", "WS-116327-0", "WS-116518-5"};
    for (const ErrorCode& c : set.codes) {
        const std::string tag = "error_codes.json " + c.id + ": ";
        CHECK(!c.source.empty(), tag + "Quelle vorhanden");
        CHECK(c.level == (community.count(c.id) ? "community" : "doc"), tag + "Stufe passt (nur die Codes ohne Sony-Seite sind community)");
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
    // Textregel (Marc, 10. Nov. 2026): kein Eintrag nennt je ein Update der Systemsoftware/Firmware, weder als Schritt noch als Hinweis
    // (die Lader lehnen so einen Text ab, siehe test_text_policy.cpp). Bei Spielen auf einer gejailbreakten Konsole steht stattdessen der
    // Hinweis auf einen Backport fuer die genutzte Firmware oder AMPR Emu / AMPR Emu NG (Marc, 9. Nov.).
    for (const ErrorCode& c : set.codes) {
        std::string all = c.title_de + "\n" + c.title_en + "\n" + c.text_de + "\n" + c.text_en;
        for (const auto* v : {&c.steps_de, &c.steps_en, &c.notes_de, &c.notes_en})
            for (const std::string& s : *v) all += "\n" + s;
        CHECK(medic::find_system_update_advice(all).empty(), "Textregel: " + c.id + " nennt kein Update der Systemsoftware/Firmware: " + medic::find_system_update_advice(all));
        CHECK(!mentions(c.notes_de, "fehlt hier mit Absicht") && !mentions(c.notes_en, "left out on purpose"), "Textregel: " + c.id + " erklaert keine Auslassung eines Update-Schritts");
    }
    for (const char* id : {"NP-102944-0", "NP-102945-1", "CE-108889-4", "WS-116328-1", "CE-107520-5", "CE-106485-4"}) {
        const ErrorCode* c = set.find(id);
        CHECK(c != nullptr, std::string("Spiele: ") + id + " vorhanden");
        if (!c) continue;
        const std::string tag = std::string("Spiele: ") + id + ": ";
        CHECK(mentions(c->steps_de, "Backport") && mentions(c->steps_en, "backport"), tag + "Schritt nennt den Backport fuer die genutzte Firmware");
        CHECK(mentions(c->steps_de, "AMPR Emu NG") && mentions(c->steps_en, "AMPR Emu NG") && mentions(c->steps_de, "AMPR Emu oder") && mentions(c->steps_en, "AMPR Emu or"),
              tag + "Schritt nennt AMPR Emu und AMPR Emu NG");
        CHECK(mentions(c->steps_de, "libSceAmpr.sprx") && mentions(c->steps_de, "fakelib/") && mentions(c->steps_de, "ShadowMount+"), tag + "Einrichtung laut README von AMPR Emu NG");
        CHECK(mentions(c->notes_de, "Szene (nicht von Sony)") && mentions(c->notes_en, "scene (not from Sony)"), tag + "Szenewissen ist als nicht von Sony gekennzeichnet");
        CHECK(mentions(c->notes_de, "nicht belegt") && mentions(c->notes_en, "not documented"), tag + "Hinweis: die Zuordnung des Codes zu diesem Fall ist nicht belegt");
        CHECK(mentions(c->notes_de, "stellt weder Spiele noch Backports bereit"), tag + "Medic liefert keine Spiele/Backports (de)");
        CHECK(mentions(c->notes_en, "neither games nor backports"), tag + "Medic liefert keine Spiele/Backports (en)");
        CHECK(std::find(c->more_sources.begin(), c->more_sources.end(), "https://github.com/quer3q/ampr-emu-ng") != c->more_sources.end(), tag + "AMPR-Emu-NG-Quelle genannt");
    }
    // Eintraege, deren Sony-Kette frueher ein Systemupdate enthielt, behalten ihre uebrigen Schritte
    for (const char* id : {"CE-100096-6", "CE-106487-6", "CE-107750-0", "CE-109573-5", "CE-117740-0", "CE-108262-9", "CE-107857-8", "CE-100006-7"}) {
        const ErrorCode* c = set.find(id);
        CHECK(c != nullptr && !c->steps_de.empty(), std::string("Schritte bleiben: ") + id);
    }

    // Pruefdatum und Fingerabdruck (tools/check_error_sources.py): jeder Eintrag hat ein Datum, jede Sony-Seite einen Fingerabdruck
    for (const ErrorCode& c : set.codes) {
        const std::string tag = "error_codes.json " + c.id + ": ";
        CHECK(c.checked.size() == 10, tag + "Pruefdatum");
        CHECK(c.level == "doc" ? c.source_sha256.size() == 64 : c.source_sha256.empty(), tag + "Fingerabdruck nur bei Sony-Seiten, und dort immer");
        if (c.id.rfind("0x", 0) == 0) CHECK(c.level == "community", tag + "Hex-Fehler haben keine Sony-Seite");
    }

    // Welche Angaben der Konsole ein Eintrag zusaetzlich zeigt: Zuordnung steht in der Datei, nicht im Programm
    const std::map<std::string, std::vector<std::string>> expected_context = {
        {"NP-102944-0", {"fw", "need_backport"}}, {"NP-102945-1", {"fw", "need_backport"}}, {"CE-108889-4", {"fw", "need_backport"}},
        {"WS-116328-1", {"fw", "need_backport"}}, {"CE-107520-5", {"fw", "need_backport"}}, {"CE-106485-4", {"fw", "need_backport"}},
        {"CE-100028-1", {"storage"}}, {"CE-107872-5", {"storage"}}, {"CE-107527-2", {"storage"}}, {"CE-107891-6", {"storage"}},
        {"CE-100006-7", {"drives"}}, {"CE-109737-7", {"drives"}}, {"CE-108262-9", {"smp"}},
        {"NP-102946-2", {"storage"}}, {"CE-107928-7", {"storage"}}, {"CE-107958-0", {"drives"}}};  // 13. Nov. 2026
    for (const ErrorCode& c : set.codes) {
        auto it = expected_context.find(c.id);
        CHECK(c.context == (it == expected_context.end() ? std::vector<std::string>() : it->second), "error_codes.json " + c.id + ": context wie vorgesehen");
    }
}

}  // namespace

// libmedic/newest.hpp: es gilt die neuere db_version, nicht die erste Datei (Konsolenfehler 13. Nov. 2026: alte Kopie verdeckte die neue Lieferung).
void newest_tests() {
    namespace fsys = std::filesystem;
    const fsys::path dir = fsys::temp_directory_path() / "medic_newest_test";
    fsys::remove_all(dir);
    fsys::create_directories(dir);
    auto write = [&](const char* name, const std::string& version) {
        json root = {{"schema_version", 1}, {"db_version", version}, {"codes", json::array({good_entry()})}};
        std::ofstream(dir / name) << root.dump();
        return (dir / name).string();
    };
    auto load = [](const std::string& p) { return medic::load_error_codes_from_file(p); };
    const std::string old_copy = write("old.json", "2026.11.9"), fresh = write("new.json", "2026.11.38"), same = write("same.json", "2026.11.9");
    std::string chosen;
    std::vector<std::string> errors;
    auto s = medic::load_newest<ErrorCodeSet>({old_copy, fresh}, load, &chosen, &errors);
    CHECK(chosen == fresh && s.db_version == "2026.11.38" && errors.empty(), "load_newest: die neuere Version gewinnt, auch wenn die aeltere zuerst steht (9 < 38 zahlenweise)");
    s = medic::load_newest<ErrorCodeSet>({fresh, old_copy}, load, &chosen);
    CHECK(chosen == fresh, "load_newest: Reihenfolge der Orte aendert nichts");
    s = medic::load_newest<ErrorCodeSet>({old_copy, same}, load, &chosen);
    CHECK(chosen == old_copy, "load_newest: bei Gleichstand gilt der fruehere Ort");
    s = medic::load_newest<ErrorCodeSet>({(dir / "fehlt.json").string(), fresh}, load, &chosen, &errors);
    CHECK(chosen == fresh && errors.empty(), "load_newest: fehlende Datei wird uebersprungen");
    std::ofstream(dir / "kaputt.json") << "{ nicht json";
    s = medic::load_newest<ErrorCodeSet>({(dir / "kaputt.json").string(), old_copy}, load, &chosen, &errors);
    CHECK(chosen == old_copy && errors.size() == 1, "load_newest: ungueltige erste Datei wird gemeldet, die gueltige zweite genommen");
    s = medic::load_newest<ErrorCodeSet>({(dir / "fehlt.json").string()}, load, &chosen);
    CHECK(chosen.empty() && s.codes.empty(), "load_newest: ohne brauchbare Datei bleibt das Ergebnis leer");

    // load_newest_peek: dieselbe Wahl, aber nur die gewaehlte Datei wird vollstaendig geladen (Konsole: die veraltete Kopie kostete >50 % der Ladezeit)
    int loads = 0;
    auto counting = [&](const std::string& p) {
        ++loads;
        return medic::load_error_codes_from_file(p);
    };
    auto peek = [](const std::string& p) { return medic::peek_db_version(p); };
    errors.clear();
    s = medic::load_newest_peek<ErrorCodeSet>({old_copy, fresh}, counting, peek, &chosen, &errors);
    CHECK(chosen == fresh && s.db_version == "2026.11.38" && errors.empty() && loads == 1, "load_newest_peek: neuere Version gewinnt, nur eine Datei wird geladen");
    loads = 0;
    s = medic::load_newest_peek<ErrorCodeSet>({fresh, old_copy}, counting, peek, &chosen);
    CHECK(chosen == fresh && loads == 1, "load_newest_peek: Reihenfolge der Orte aendert nichts");
    s = medic::load_newest_peek<ErrorCodeSet>({old_copy, same}, counting, peek, &chosen);
    CHECK(chosen == old_copy, "load_newest_peek: bei Gleichstand gilt der fruehere Ort");
    s = medic::load_newest_peek<ErrorCodeSet>({(dir / "fehlt.json").string(), fresh}, counting, peek, &chosen, &errors);
    CHECK(chosen == fresh && errors.empty(), "load_newest_peek: fehlende Datei wird uebersprungen");
    errors.clear();
    s = medic::load_newest_peek<ErrorCodeSet>({(dir / "kaputt.json").string(), old_copy}, counting, peek, &chosen, &errors);
    CHECK(chosen == old_copy && errors.size() == 1, "load_newest_peek: Datei ohne lesbares JSON wird gemeldet, die gueltige genommen");
    // Neueste Datei ist lesbar, aber ungueltig (leere Codeliste): die naechstniedrigere kommt dran, der Fehler wird gemeldet
    {
        json bad = {{"schema_version", 1}, {"db_version", "2026.11.99"}, {"codes", json::array()}, {"unbekannt", 1}};
        std::ofstream(dir / "bad_newest.json") << bad.dump();
    }
    errors.clear();
    s = medic::load_newest_peek<ErrorCodeSet>({(dir / "bad_newest.json").string(), old_copy}, counting, peek, &chosen, &errors);
    CHECK(chosen == old_copy && errors.size() == 1 && s.db_version == "2026.11.9", "load_newest_peek: ungueltige neueste Datei: Rueckfall auf die naechste, Fehler gemeldet");
    s = medic::load_newest_peek<ErrorCodeSet>({(dir / "fehlt.json").string()}, counting, peek, &chosen);
    CHECK(chosen.empty() && s.codes.empty(), "load_newest_peek: ohne brauchbare Datei bleibt das Ergebnis leer");
    CHECK(medic::peek_db_version(fresh) == "2026.11.38", "peek_db_version liest die Version");
    bool threw = false;
    try {
        std::ofstream(dir / "keine_version.json") << R"({"schema_version":1})";
        (void)medic::peek_db_version((dir / "keine_version.json").string());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw, "peek_db_version wirft ohne db_version");
    fsys::remove_all(dir);
}

void error_code_tests() {
    schema_tests();
    content_tests();
    newest_tests();
}
