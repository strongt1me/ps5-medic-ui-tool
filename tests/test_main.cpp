// Minimaler Test-Runner ohne Fremdabhaengigkeiten.
//  1. Unit-Tests (Versionsvergleich, Operatoren, Schema-Pruefung, Pfadschutz)
//  2. Fixture-Tests: jeder Ordner in tests/fixtures/ wird mit knowledge/rules.json geprueft;
//     expect.json {"expect":["regel.id", ...]} muss exakt der Menge der Befunde entsprechen.
#include "medic/collectors.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"
#include "check.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using nlohmann::json;

int g_fail = 0, g_pass = 0;

static bool load_fails(const std::string& text) {
    try {
        medic::load_rules_from_string(text);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

static std::string rules_with(const std::string& when, const std::string& extra = "") {
    return R"({"schema_version":1,"db_version":"t","rules":[{"id":"t.rule","severity":"info","when":)" + when +
           R"(,"text":{"de":"d","en":"e"},"source":"https://example.org","verified":"doc")" + extra + "}]}";
}

static std::set<std::string> ids_for(const std::string& rules_json, const medic::Facts& f) {
    std::set<std::string> ids;
    for (auto& x : medic::evaluate(medic::load_rules_from_string(rules_json), f)) ids.insert(x.rule_id);
    return ids;
}

static void unit_tests() {
    using medic::compare_versions;
    CHECK(compare_versions("1.07", "1.07") == 0, "gleiche Version");
    CHECK(compare_versions("1.10", "1.07") > 0, "1.10 > 1.07");
    CHECK(compare_versions("1.06", "1.07") < 0, "1.06 < 1.07");
    CHECK(compare_versions("13.60", "9.00") > 0, "numerisch, nicht lexikografisch");
    CHECK(compare_versions("4.03", "4.3") == 0, "fuehrende Nullen egal");
    CHECK(compare_versions("1.7beta3", "1.7") == 0, "Suffix wird ignoriert");

    CHECK(medic::fw_in_range("4.03", ">=4.00 <=7.61"), "FW im Bereich");
    CHECK(!medic::fw_in_range("9.00", ">=4.00 <=7.61"), "FW ausserhalb");
    CHECK(medic::fw_in_range("", ">=4.00"), "unbekannte FW unterdrueckt nicht");
    CHECK(medic::fw_in_range("13.60", "=13.60"), "exakte FW");

    // Operatoren
    medic::Facts f;
    f.set("kstuff.version", "1.06");
    f.set("proc.running", json::array({"etahen", "kstuff"}));
    f.set("smp.config.debug", "0");
    f.set("smp.images", json::array({{{"name", "A (1).ffpfs"}, {"ext", ".ffpfs"}}, {{"name", "B.exfat"}, {"ext", ".exfat"}}}));
    CHECK(ids_for(rules_with(R"({"fact":"kstuff.version","lt":"1.07"})"), f).count("t.rule"), "lt");
    CHECK(!ids_for(rules_with(R"({"fact":"kstuff.version","gt":"1.07"})"), f).count("t.rule"), "gt negativ");
    CHECK(ids_for(rules_with(R"({"fact":"proc.running","contains":"etahen"})"), f).count("t.rule"), "contains (Array)");
    CHECK(ids_for(rules_with(R"({"fact":"smp.config.debug","eq":"0"})"), f).count("t.rule"), "eq");
    CHECK(ids_for(rules_with(R"({"fact":"smp.config.missing","eq":"1","default":"1"})"), f).count("t.rule"), "default");
    CHECK(!ids_for(rules_with(R"({"fact":"smp.config.missing","eq":"1"})"), f).count("t.rule"), "fehlender Fakt -> falsch");
    CHECK(ids_for(rules_with(R"({"fact":"smp.config.missing","exists":false})"), f).count("t.rule"), "exists:false");
    CHECK(ids_for(rules_with(R"({"not":{"fact":"proc.running","contains":"backpork"}})"), f).count("t.rule"), "not");
    CHECK(ids_for(rules_with(R"({"any":[{"fact":"smp.config.debug","eq":"9"},{"fact":"smp.config.debug","eq":"0"}]})"), f).count("t.rule"), "any");
    // Array-Bindung: beide Bedingungen muessen auf dasselbe Element zutreffen
    CHECK(ids_for(rules_with(R"({"all":[{"fact":"smp.images[].name","matches":"[()]"},{"fact":"smp.images[].ext","in":[".ffpfs"]}]})"), f).count("t.rule"),
          "Array: gleiches Element trifft");
    CHECK(!ids_for(rules_with(R"({"all":[{"fact":"smp.images[].name","matches":"[()]"},{"fact":"smp.images[].ext","in":[".exfat"]}]})"), f).count("t.rule"),
          "Array: verschiedene Elemente duerfen nicht kombiniert werden");
    CHECK(ids_for(rules_with(R"({"fact":"proc.running","count_matches":{"regex":"^etahen$","gte":1}})"), f).count("t.rule"), "count_matches >= 1");
    CHECK(!ids_for(rules_with(R"({"fact":"proc.running","count_matches":{"regex":"^etahen$","gte":2}})"), f).count("t.rule"), "count_matches < gte");
    CHECK(load_fails(rules_with(R"({"fact":"proc.running[]","count_matches":{"regex":"x","gte":1}})")), "count_matches mit [] ungueltig");
    CHECK(load_fails(rules_with(R"({"fact":"proc.running","count_matches":{"regex":"x","gte":0}})")), "count_matches gte >= 1");
    // applies_fw
    medic::Facts fw;
    fw.set("fw.version", "4.03");
    CHECK(!ids_for(rules_with(R"({"fact":"fw.version","exists":true})", R"(,"applies_fw":">=7.00")"), fw).count("t.rule"), "applies_fw filtert");

    // Schema-Pruefung
    CHECK(load_fails("kein json"), "ungueltiges JSON");
    CHECK(load_fails(R"({"schema_version":2,"db_version":"x","rules":[]})"), "falsche schema_version");
    CHECK(load_fails(rules_with(R"({"fact":"x","matches":"("})")), "kaputte Regex");
    CHECK(load_fails(rules_with(R"({"fact":"x","eqq":"1"})")), "Tippfehler im Operator");
    CHECK(load_fails(rules_with(R"({"fact":"x"})")), "Blatt ohne Operator");
    CHECK(load_fails(rules_with(R"({"fact":"x","eq":"1"})", R"(,"severity":"bad")")), "ungueltige severity");  // doppelter Schluessel: letzter Wert gewinnt
    CHECK(load_fails(R"({"schema_version":1,"db_version":"x","rules":[{"id":"A B"}]})"), "ungueltige ID");
    {
        std::string dup = rules_with(R"({"fact":"x","exists":true})");
        std::string one = dup.substr(dup.find("{\"id\""), dup.rfind("]}") - dup.find("{\"id\""));
        std::string two = R"({"schema_version":1,"db_version":"x","rules":[)" + one + "," + one + "]}";
        CHECK(load_fails(two), "doppelte Regel-ID");
    }

    // Die ausgelieferte Wissensbasis muss gueltig sein
    try {
        auto rs = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        CHECK(!rs.rules.empty(), "rules.json enthaelt Regeln");
    } catch (const std::exception& e) {
        CHECK(false, std::string("rules.json laden: ") + e.what());
    }
}

static void platform_tests() {
    const std::string fx = std::string(MEDIC_SOURCE_DIR) + "/tests/fixtures/b2_pos";
    auto p = medic::make_snapshot_platform(fx);
    CHECK(p->sys().firmware() == "4.03", "Firmware aus system.json");
    CHECK(p->fs().is_dir("/data/homebrew"), "Verzeichnis sichtbar");
    CHECK(!p->fs().exists("/data/../procs.json"), "'..' wird abgelehnt");
    CHECK(!p->fs().exists("data/homebrew"), "relative Pfade werden abgelehnt");
    std::string s;
    CHECK(p->fs().read_file("/data/shadowmount/config.ini", s) && s.find("debug=1") != std::string::npos, "Datei lesen");
    CHECK(!p->fs().read_file("/data/shadowmount/nope.ini", s), "fehlende Datei");
    CHECK(medic::make_snapshot_platform(fx + "_gibt_es_nicht")->procs().running().empty(), "fehlender Snapshot = leerer Zustand");
}

// Die Fakten der Regeln A2, A4, C1, C2 selbst (nicht nur das Ergebnis der Regeln): Grenzfaelle und fehlende Quellen.
static void new_fact_tests() {
    auto facts_of = [](const char* fixture) {
        return medic::collect_all(*medic::make_snapshot_platform(std::string(MEDIC_SOURCE_DIR) + "/tests/fixtures/" + fixture));
    };
    auto num = [](const medic::Facts& f, const char* k) -> long {
        const json* v = f.get(k);
        return v && v->is_number() ? v->get<long>() : -1;
    };
    auto str = [](const medic::Facts& f, const char* k) -> std::string {
        const json* v = f.get(k);
        return v && v->is_string() ? v->get<std::string>() : std::string("<fehlt>");
    };

    {  // A2: Sekunden zwischen Konsolenstart und dem fruehesten Prozess der Jailbreak-Kette
        auto f = facts_of("a2_pos");
        CHECK(num(f, "jb.seconds_after_boot") == 45 && str(f, "jb.first_process") == "elfldr", "A2: 45 s nach dem Start, erster Prozess elfldr");
        auto g = facts_of("a2_neg");
        CHECK(num(g, "jb.seconds_after_boot") == 187 && str(g, "jb.first_process") == "kstuff", "A2: der fruehste Prozess zaehlt, auch wenn elfldr spaeter startet");
        CHECK(!facts_of("a2_neg_noboot").get("jb.seconds_after_boot"), "A2: ohne Konsolenstart kein Fakt");
        CHECK(!facts_of("a2_neg_nostarts").get("jb.seconds_after_boot"), "A2: ohne Startzeiten der Prozesse kein Fakt");
    }
    {  // A4: Port 9021 nur mit eindeutiger Antwort; ohne Angabe fehlt der Fakt
        CHECK(str(facts_of("a4_pos"), "net.port_9021") == "closed", "A4: Port zu");
        CHECK(str(facts_of("a4_neg"), "net.port_9021") == "open", "A4: Port offen");
        CHECK(!facts_of("a3_neg").get("net.port_9021"), "A4: ohne Angabe im Snapshot kein Fakt");
    }
    {  // C1: Version aus dem Log (letzte Startzeile), sonst aus der Plugin-Datei
        auto f = facts_of("c1_pos");
        CHECK(str(f, "ps5theme.version") == "1.45" && str(f, "ps5theme.source") == "log", "C1: Version aus dem Log");
        auto p = facts_of("c1_pos_plugin");
        CHECK(str(p, "ps5theme.version") == "1.40" && str(p, "ps5theme.source") == "plugin", "C1: Version aus der Plugin-Datei");
        CHECK(str(facts_of("c1_neg_updated"), "ps5theme.version") == "1.55", "C1: die letzte Startzeile gilt");
        CHECK(str(facts_of("c1_neg_release"), "ps5theme.version") == "1.23", "C1: Format [ps5-theme] 1.23 starting");
        CHECK(!facts_of("a3_neg").get("ps5theme.version"), "C1: ohne ps5-theme kein Fakt");
    }
    {  // C2: nur Apps mit grosser Reservierung und kleinem Inhalt
        auto f = facts_of("c2_pos");
        const json* v = f.get("apps.oversized");
        CHECK(v && v->is_array() && v->size() == 1, "C2: ein verdaechtige App");
        if (v && v->is_array() && v->size() == 1) {
            const json& a = (*v)[0];
            // eboot.bin (2048) + icon0.png (512) + param.json (ein paar hundert Byte)
            CHECK(a["titleid"] == "PPSA98427" && a["name"] == "CD Player" && a["reserved_mib"] == 16384 && a["content_bytes"] > 2560 &&
                      a["content_bytes"] < 3584,
                  "C2: Titel, Name, Reservierung und Inhaltsgroesse");
        }
        auto neg = facts_of("c2_neg");
        const json* none = neg.get("apps.oversized");
        CHECK(none && none->is_array() && none->empty(), "C2: 256 MiB sind unauffaellig");
    }
    {  // C3: freier Platz in Prozent
        auto c3 = facts_of("c3_pos");
        const json* m = c3.get("storage.mounts");
        CHECK(m && m->is_array() && !m->empty() && (*m)[0]["free_pct"].get<double>() < 5.0, "C3: Fakt free_pct unter 5 %");
    }
}

static void fixture_tests() {
    const fs::path root = fs::path(MEDIC_SOURCE_DIR) / "tests" / "fixtures";
    auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    std::vector<fs::path> dirs;
    for (auto& e : fs::directory_iterator(root))
        if (e.is_directory()) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    CHECK(!dirs.empty(), "Fixtures vorhanden");

    // Jede Regel braucht ein Positiv-Fixture (Regel feuert) und ein Negativ-Fixture:
    // expect.json {"neg_for":["regel.id"]} markiert den "Beinahe-Treffer", bei dem die Regel NICHT feuern darf.
    std::set<std::string> fired_somewhere, neg_declared;
    for (auto& d : dirs) {
        std::ifstream in(d / "expect.json");
        std::ostringstream ss;
        ss << in.rdbuf();
        json ex = json::parse(ss.str(), nullptr, false);
        if (!ex.is_object() || !ex.contains("expect") || !ex["expect"].is_array()) {
            CHECK(false, d.filename().string() + ": expect.json fehlt oder ist ungueltig");
            continue;
        }
        std::set<std::string> want, got;
        for (auto& v : ex["expect"]) want.insert(v.get<std::string>());
        auto p = medic::make_snapshot_platform(d.string());
        for (auto& f : medic::evaluate(rules, medic::collect_all(*p))) got.insert(f.rule_id);
        CHECK(want == got, "Fixture " + d.filename().string() + ": Befunde stimmen mit expect.json ueberein");
        if (want != got) {
            std::cerr << "  erwartet:";
            for (auto& w : want) std::cerr << " " << w;
            std::cerr << "\n  erhalten:";
            for (auto& g : got) std::cerr << " " << g;
            std::cerr << "\n";
        }
        for (auto& g : got) fired_somewhere.insert(g);
        if (ex.contains("neg_for"))
            for (auto& v : ex["neg_for"]) {
                neg_declared.insert(v.get<std::string>());
                CHECK(!got.count(v.get<std::string>()), "Fixture " + d.filename().string() + ": neg_for-Regel feuert nicht");
            }
    }
    for (auto& r : rules.rules) {
        CHECK(fired_somewhere.count(r.id), "Regel " + r.id + " hat ein Positiv-Fixture");
        CHECK(neg_declared.count(r.id), "Regel " + r.id + " hat ein Negativ-Fixture (neg_for)");
    }
}

int main() {
    unit_tests();
    platform_tests();
    new_fact_tests();
    fixture_tests();
    fix_tests();
    ui_tests();
    std::cout << g_pass << " Pruefungen ok, " << g_fail << " fehlgeschlagen\n";
    return g_fail ? 1 : 0;
}
