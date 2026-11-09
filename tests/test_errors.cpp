// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des Fehlercode-Nachschlagewerks (knowledge/error_codes.json): Schema, Suche, Verweise auf Regeln und die eingebettete Kopie.
#include "check.hpp"
#include "medic/error_codes.hpp"
#include "medic/rules.hpp"
#include "medic_ui/error_data.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using namespace medic;
using json = nlohmann::json;

namespace {

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string no_cr_trim(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

// Die UI-Schrift hat nur ASCII und Latin-1 (und was in den UI-Texten steht): jedes Zeichen darueber waere ein Kaestchen.
bool only_latin1(const std::string& utf8) {
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(utf8[i]);
        if (ch < 0x80) continue;
        if ((ch == 0xC2 || ch == 0xC3) && i + 1 < utf8.size() && (static_cast<unsigned char>(utf8[i + 1]) & 0xC0) == 0x80) {
            ++i;
            continue;
        }
        return false;
    }
    return true;
}

bool throws(const std::string& text) {
    try {
        load_error_db_from_string(text);
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

// Kleinste gueltige Datei, einzelne Felder lassen sich ueberschreiben.
json mini() {
    return json::parse(R"({
      "db_version": "1", "source": "x", "url_template": "https://example.org/{code_lower}/",
      "prefixes": {"CE": {"de": "a", "en": "a"}},
      "kinds": {"k": {"title": {"de": "t", "en": "t"}, "steps": {"de": ["s"], "en": ["s"]}}},
      "codes": [{"code": "CE-100000-1", "kind": "k", "text": {"de": "d", "en": "e"}}]
    })");
}

}  // namespace

void error_tests() {
    const fs::path root = MEDIC_SOURCE_DIR;
    const std::string file_text = read_all(root / "knowledge" / "error_codes.json");
    ErrorDb db;
    bool loaded = true;
    try {
        db = load_error_db_from_string(file_text);
    } catch (const std::exception& e) {
        loaded = false;
        std::cerr << "error_codes.json: " << e.what() << "\n";
    }
    CHECK(loaded, "Fehlercodes: knowledge/error_codes.json laedt und besteht die Schemapruefung");
    if (!loaded) return;

    // Umfang und Vollstaendigkeit der Liste (Sony nennt fuer die PS5 155 Codes, Stand 9. Nov. 2026)
    CHECK(db.codes.size() == 155, "Fehlercodes: alle 155 PS5-Codes der offiziellen Liste");
    CHECK(db.prefixes.size() == 7 && db.prefixes.front().id == "CE", "Fehlercodes: sieben Familien, CE zuerst");
    CHECK(only_latin1(file_text), "Fehlercodes: nur ASCII und Latin-1 (die UI-Schrift hat nicht mehr)");
    CHECK(file_text.find("http://") == std::string::npos, "Fehlercodes: keine unverschluesselten Links");

    std::set<std::string> used_kinds, used_prefixes;
    for (const auto& c : db.codes) {
        used_kinds.insert(c.kind);
        used_prefixes.insert(code_prefix(c.code));
        CHECK(normalize_code(c.code) == c.code, "Fehlercodes: kanonische Schreibweise " + c.code);
        CHECK(c.text_de != c.text_en || c.code.rfind("E2", 0) == 0, "Fehlercodes: Deutsch und Englisch unterscheiden sich bei " + c.code);
    }
    for (const auto& k : db.kinds) {
        CHECK(used_kinds.count(k.id) == 1, "Fehlercodes: Art '" + k.id + "' wird verwendet");
        CHECK(k.id == "info" || !k.steps_de.empty(), "Fehlercodes: Art '" + k.id + "' hat Schritte");
        CHECK(k.steps_de.size() == k.steps_en.size(), "Fehlercodes: gleich viele Schritte in " + k.id);
        CHECK(k.steps_de.size() <= 6, "Fehlercodes: hoechstens sechs Schritte in " + k.id + " (mehr passt nicht auf den Bildschirm)");
        CHECK(k.homebrew_de.empty() == k.homebrew_en.empty(), "Fehlercodes: Homebrew-Hinweis in beiden Sprachen oder in keiner (" + k.id + ")");
        CHECK(k.homebrew_de.size() <= 330 && k.homebrew_en.size() <= 330, "Fehlercodes: Homebrew-Hinweis nicht laenger als fuenf Zeilen (" + k.id + ")");
    }
    for (const auto& p : db.prefixes) CHECK(used_prefixes.count(p.id) == 1, "Fehlercodes: Familie " + p.id + " hat Codes");

    // Verweise auf Regeln muessen es geben (Regel umbenannt oder entfernt faellt sonst nicht auf)
    {
        const RuleSet rs = load_rules_from_file((root / "knowledge" / "rules.json").string());
        std::set<std::string> ids;
        for (const auto& r : rs.rules) ids.insert(r.id);
        for (const auto& k : db.kinds)
            for (const auto& id : k.related_rules) CHECK(ids.count(id) == 1, "Fehlercodes: Regel '" + id + "' aus Art '" + k.id + "' gibt es in rules.json");
    }

    // Stichproben
    {
        const auto* c = find_code(db, "CE-108255-1");
        CHECK(c && c->kind == "app_error" && db.kind_of(*c) && db.kind_of(*c)->check, "Fehlercodes: CE-108255-1 ist ein App-Fehler mit Check-up-Hinweis");
        const auto* s = find_code(db, "SU-101495-0");
        CHECK(s && s->kind == "info" && db.kind_of(*s)->steps_de.empty(), "Fehlercodes: SU-101495-0 ist kein Fehler");
        const auto* w = find_code(db, "WS-116331-5");
        CHECK(w && w->kind == "suspended" && !db.kind_of(*w)->check, "Fehlercodes: Sperren gehoeren zu Sony, der Check-up hilft nicht");
        const auto* u = find_code(db, "NP-102944-0");
        CHECK(u && u->kind == "system_update" && db.kind_of(*u)->homebrew_de.find("Jailbreak") != std::string::npos,
              "Fehlercodes: Systemupdate-Fehler warnt vor dem Ende des Jailbreaks");
        const auto* n = find_code(db, "CE-118527-4");
        CHECK(n && n->kind == "network" && db.kind_of(*n)->homebrew_de.find("DNS") != std::string::npos, "Fehlercodes: DNS-Fehler verweist auf DNS-Sperren");
    }

    // Eingaben vereinheitlichen
    CHECK(normalize_code("ce-108255-1") == "CE-108255-1", "Fehlercodes: Kleinschreibung");
    CHECK(normalize_code("CE1082551") == "CE-108255-1", "Fehlercodes: ohne Bindestriche");
    CHECK(normalize_code("  Ce 108255 1 ") == "CE-108255-1", "Fehlercodes: Leerzeichen");
    CHECK(normalize_code("e2-8223b400") == "E2-8223B400", "Fehlercodes: E2-Codes mit Hex");
    CHECK(normalize_code("CE-108255") == "" && normalize_code("108255-1") == "" && normalize_code("") == "" && normalize_code("CE-10825x-1") == "",
          "Fehlercodes: unvollstaendige oder ungueltige Eingaben ergeben nichts");
    CHECK(normalize_code("E2-8223G400").empty(), "Fehlercodes: Hex-Teil darf nur 0-9 und A-F enthalten");
    CHECK(code_prefix("ce-999999-9") == "CE" && code_prefix("12") == "" && code_prefix("") == "", "Fehlercodes: Familie aus der Eingabe");

    CHECK(find_code(db, "ce1082551") != nullptr && find_code(db, "E2-8223b400") != nullptr, "Fehlercodes: Suche ist tolerant bei Schreibweisen");
    CHECK(find_code(db, "CE-999999-9") == nullptr && find_code(db, "Spiel") == nullptr, "Fehlercodes: unbekannter Code liefert nichts");

    // Teilsuche
    CHECK(search_codes(db, "108255").size() == 1, "Fehlercodes: Teilsuche mit sechs Ziffern trifft genau einen Code");
    CHECK(search_codes(db, "ce").empty() && search_codes(db, "1").empty(), "Fehlercodes: unter drei Zeichen wird nicht gesucht");
    CHECK(search_codes(db, "1029").size() > 3, "Fehlercodes: kurze Ziffernfolge trifft mehrere");
    CHECK(search_codes(db, "zzzzzz").empty(), "Fehlercodes: Unsinn trifft nichts");
    CHECK(codes_with_prefix(db, "CE").size() + codes_with_prefix(db, "NP").size() + codes_with_prefix(db, "WS").size() +
                  codes_with_prefix(db, "NW").size() + codes_with_prefix(db, "WV").size() + codes_with_prefix(db, "SU").size() +
                  codes_with_prefix(db, "E2").size() ==
              db.codes.size(),
          "Fehlercodes: jede Familie zusammen ergibt alle Codes");

    // Link zur offiziellen Seite
    CHECK(support_url(db, "CE-108255-1") == "https://www.playstation.com/en-us/support/error-codes/ps5/ce-108255-1/", "Fehlercodes: Link zur Sony-Seite");
    CHECK(support_url(db, "E2-8223B400").find("e2-8223b400") != std::string::npos, "Fehlercodes: Link in Kleinschreibung");

    // Strenge Schemapruefung beim Laden
    {
        CHECK(!throws(mini().dump()), "Fehlercodes: kleinste Datei ist gueltig");
        CHECK(throws("{nicht json"), "Fehlercodes: kein JSON wird abgelehnt");
        CHECK(throws("[]"), "Fehlercodes: Wurzel muss ein Objekt sein");
        json j = mini();
        j["codes"].push_back(j["codes"][0]);
        CHECK(throws(j.dump()), "Fehlercodes: doppelter Code wird abgelehnt");
        j = mini();
        j["codes"][0]["kind"] = "gibt-es-nicht";
        CHECK(throws(j.dump()), "Fehlercodes: unbekannte Art wird abgelehnt");
        j = mini();
        j["codes"][0]["code"] = "ce-100000-1";
        CHECK(throws(j.dump()), "Fehlercodes: Code muss kanonisch geschrieben sein");
        j = mini();
        j["codes"][0]["code"] = "NP-100000-1";
        CHECK(throws(j.dump()), "Fehlercodes: Familie muss in prefixes stehen");
        j = mini();
        j["kinds"]["k"]["steps"]["en"] = json::array();
        CHECK(throws(j.dump()), "Fehlercodes: Schritte in beiden Sprachen gleich viele");
        j = mini();
        j["kinds"]["k"]["homebrew"] = {{"de", "nur deutsch"}, {"en", ""}};
        CHECK(throws(j.dump()), "Fehlercodes: Homebrew-Hinweis braucht beide Sprachen");
        j = mini();
        j["codes"][0]["text"]["en"] = "";
        CHECK(throws(j.dump()), "Fehlercodes: leerer Text wird abgelehnt");
        j = mini();
        j["url_template"] = "https://example.org/fest/";
        CHECK(throws(j.dump()), "Fehlercodes: Link-Vorlage braucht den Platzhalter");
        j = mini();
        j.erase("kinds");
        CHECK(throws(j.dump()), "Fehlercodes: fehlende Arten werden abgelehnt");
        j = mini();
        j["codes"] = json::array();
        CHECK(throws(j.dump()), "Fehlercodes: leere Codeliste wird abgelehnt");
    }

    // Eingebettete Kopie fuer die UI
    CHECK(no_cr_trim(file_text) == no_cr_trim(medic::ui::kErrorCodesJson), "Fehlercodes: eingebettete Kopie entspricht error_codes.json (tools/embed_error_codes.py ausfuehren)");
    CHECK(medic::ui::embedded_error_db().codes.size() == db.codes.size(), "Fehlercodes: die UI sieht alle Codes");
}
