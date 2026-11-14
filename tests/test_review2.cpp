// SPDX-License-Identifier: GPL-3.0-or-later
// Tests zu den Funden des zweiten Gesamt-Reviews (13. Nov. 2026): Planer der Fixes (Wertebereich, veraltete Zeilen, Groessengrenze, Quarantaene-Kollisionen,
// auskommentierte Zeilen), Schwaerzung von Zugangsdaten, Spiele-Zuordnung, Firmware-Bereiche, Regel-Lader.
#include "check.hpp"
#include "medic/checks.hpp"
#include "medic/collectors.hpp"
#include "medic/fixes.hpp"
#include "medic/games.hpp"
#include "medic/report.hpp"
#include "medic/rules.hpp"
#include "testutil.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>

using namespace medic;
using nlohmann::json;
using testutil::Sandbox;
using testutil::spit;

namespace {

struct Env {
    Sandbox sb;
    std::unique_ptr<Platform> p;
    Env() : p(make_snapshot_platform(sb.dir.string())) {}
    void put(const std::string& vpath, const std::string& text) { spit(sb.real(vpath), text); }
};

bool any_warning(const FixPlan& plan, const std::string& part) {
    for (const auto& w : plan.warnings)
        if (w.find(part) != std::string::npos) return true;
    return false;
}

Finding finding_for(const std::string& fix, json params = json::object()) {
    Finding f;
    f.rule_id = "t.rule";
    f.fix = fix;
    f.fix_params = std::move(params);
    return f;
}

void planner_tests() {
    // smp.scan_depth: SM+ nimmt in v2 hoechstens depth 2 an; der Fix darf keinen groesseren Wert schreiben (sonst verwirft SM+ die ganze Datei)
    {
        Env e;
        e.put("/data/shadowmount/config.ini", "[config]\nversion = 2\n[scan]\ndepth = 1\n");
        Facts facts;
        facts.set("smp.deep_sources", json::array({json{{"path", "/data/homebrew/a/b/G"}, {"level", 3}}}));
        const json params = {{"section", "scan"}, {"key", "depth"}, {"legacy_key", "scan_depth"}, {"from", {{"array", "smp.deep_sources"}, {"field", "level"}, {"agg", "max"}}}, {"clamp_max", 2}};
        const FixPlan plan = plan_fix(finding_for("smp_set_key", params), facts, e.p->fs(), {});
        CHECK(plan.changes.size() == 1, "scan_depth: ein Plan");
        if (!plan.changes.empty()) {
            const std::string& c = plan.changes[0].content;
            CHECK(c.find("depth = 2") != std::string::npos || c.find("depth=2") != std::string::npos, "scan_depth: auf den Hoechstwert 2 begrenzt");
            CHECK(c.find("depth = 3") == std::string::npos && c.find("depth=3") == std::string::npos, "scan_depth: kein ungueltiger Wert 3");
        }
        CHECK(!plan.hint_de.empty(), "scan_depth: Hinweis, dass tiefere Ebenen unauffindbar bleiben");
    }

    // smp_translate_v1_keys: ungueltige Werte werden nicht in die v2-Sektion uebertragen, die Zeilen bleiben
    {
        Env e;
        e.put("/data/shadowmount/config.ini", "[config]\nscan_depth=3\nfan_target_temperature=70\n");
        Facts facts;
        facts.set("smp.v1_keys", json::array({json{{"key", "scan_depth"}, {"value", "3"}, {"line", 2}, {"tsection", "scan"}, {"tkey", "depth"}, {"add", false}},
                                              json{{"key", "fan_target_temperature"}, {"value", "70"}, {"line", 3}, {"tsection", "fan"}, {"tkey", "target_temperature"}, {"add", false}}}));
        const FixPlan plan = plan_fix(finding_for("smp_translate_v1_keys"), facts, e.p->fs(), {});
        CHECK(plan.changes.size() == 1, "v1-Fix: Plan mit dem gueltigen Wert");
        if (!plan.changes.empty()) {
            const std::string& c = plan.changes[0].content;
            CHECK(c.find("scan_depth=3") != std::string::npos, "v1-Fix: Zeile mit ungueltigem Wert bleibt unveraendert");
            CHECK(c.find("fan_target_temperature=70") == std::string::npos && c.find("target_temperature") != std::string::npos, "v1-Fix: gueltige Zeile wandert in die v2-Sektion");
            CHECK(c.find("[scan]") == std::string::npos, "v1-Fix: kein [scan] depth mit ungueltigem Wert angelegt");
        }
        CHECK(any_warning(plan, "ungültiger Wert"), "v1-Fix: Warnung zum uebersprungenen Wert");
    }
    // ... und eine seit dem Check-up veraenderte Datei: die falsche Zeile wird nicht entfernt
    {
        Env e;
        e.put("/data/shadowmount/config.ini", "[config]\n# neu\nscan_depth=2\ndebug=1\n");  // scan_depth steht jetzt in Zeile 3, nicht mehr in 2
        Facts facts;
        facts.set("smp.v1_keys", json::array({json{{"key", "scan_depth"}, {"value", "2"}, {"line", 2}, {"tsection", "scan"}, {"tkey", "depth"}, {"add", false}}}));
        const FixPlan plan = plan_fix(finding_for("smp_translate_v1_keys"), facts, e.p->fs(), {});
        CHECK(plan.changes.empty() && any_warning(plan, "geändert"), "v1-Fix: veraltete Zeilennummer, nichts wird entfernt");
    }

    // autoload_insert_delay: nicht ueber die Grenze von 4095 Byte des Payload Managers
    {
        Env e;
        std::string text = "elfldr.elf\nx.elf\n";
        text += std::string(4095 - text.size() - 1, 'a') + "\n";
        const std::string file = "/data/pldmgr/autoload.txt";
        e.put(file, text);
        Facts facts;
        facts.set("autoload.problems", json::array({json{{"kind", "elfldr_no_delay"}, {"file", file}}}));
        const FixPlan plan = plan_fix(finding_for("autoload_insert_delay"), facts, e.p->fs(), {});
        CHECK(text.size() == 4095 && plan.changes.empty() && any_warning(plan, "4095"), "autoload_insert_delay: Liste wuerde ueber 4095 Byte wachsen, uebersprungen");
    }

    // BackPork: Text hinter '#' zaehlt nicht als Eintrag
    {
        Env e;
        const std::string file = "/data/ps5_autoloader/autoload.txt";
        e.put(file, "kstuff.elf # nicht backpork.elf\nbackpork.elf\n");
        const FixPlan plan = plan_fix(finding_for("smp_disable_conflicting_backpork", {{"files", json::array({file})}, {"match", "backpork"}}), Facts{}, e.p->fs(), {});
        CHECK(plan.changes.size() == 1 && plan.changes[0].content == "kstuff.elf # nicht backpork.elf\n", "BackPork: aktiver kstuff-Eintrag mit Kommentar-Rest bleibt");
    }
    // Die Grenze von 4095 Byte gilt nur fuer den Payload Manager, nicht fuer die Liste des WebKit-Autoloaders
    {
        Env e;
        std::string text = "elfldr.elf\nx.elf\n";
        text += std::string(4095 - text.size() - 1, 'a') + "\n";
        const std::string file = "/data/ps5_autoloader/autoload.txt";
        e.put(file, text);
        Facts facts;
        facts.set("autoload.problems", json::array({json{{"kind", "elfldr_no_delay"}, {"file", file}}}));
        const FixPlan plan = plan_fix(finding_for("autoload_insert_delay"), facts, e.p->fs(), {});
        CHECK(plan.changes.size() == 1, "autoload_insert_delay: WebKit-Autoloader ohne 4095-Grenze");
    }

    // Quarantaene: liegt dort schon eine gleichnamige Datei, bekommt die neue einen Zaehler (der Fix bleibt ausfuehrbar)
    {
        Env e;
        e.put("/data/homebrew/bad.elf", "x");
        e.put("/data/homebrew/.medic_broken/bad.elf", "alt");
        Facts facts;
        facts.set("smp.scan_roots", json::array({"/data/homebrew"}));
        facts.set("payload.broken_elfs", json::array({json{{"path", "/data/homebrew/bad.elf"}, {"reason", "too_small"}}}));
        const FixPlan plan = plan_fix(finding_for("quarantine_broken_elf"), facts, e.p->fs(), {});
        CHECK(plan.changes.size() == 1 && plan.changes[0].to == "/data/homebrew/.medic_broken/bad.elf.2", "Quarantaene: zweite Datei gleichen Namens -> bad.elf.2");
    }

    // BackPork: eine schon auskommentierte Zeile der WebKit-Liste bleibt stehen
    {
        Env e;
        const std::string file = "/data/ps5_autoloader/autoload.txt";
        e.put(file, "# backpork.elf (bewusst aus)\nbackpork.elf\nkstuff.elf\n");
        const FixPlan plan = plan_fix(finding_for("smp_disable_conflicting_backpork", {{"files", json::array({file})}, {"match", "backpork"}}), Facts{}, e.p->fs(), {});
        CHECK(plan.changes.size() == 1, "BackPork: ein Plan");
        if (!plan.changes.empty())
            CHECK(plan.changes[0].content == "# backpork.elf (bewusst aus)\nkstuff.elf\n", "BackPork: nur die aktive Zeile entfernt, der Kommentar bleibt");
    }
}

void collector_tests() {
    // Eine 0-Byte-ELF (abgebrochene Uebertragung) ist ein Befund, ein haengender Link nicht
    Env e;
    e.put("/data/homebrew/zero.elf", "");
    e.put("/data/homebrew/three.elf", "abc");
    std::error_code ec;
    std::filesystem::create_symlink("/nonexistent/target", e.sb.real("/data/homebrew/dangling.elf"), ec);
    Facts facts;
    collect_payload_files(*e.p, facts);
    const json* broken = facts.get("payload.broken_elfs");
    bool zero = false, three = false, dangling = false;
    if (broken && broken->is_array())
        for (const auto& b : *broken) {
            const std::string path = b.value("path", std::string());
            zero = zero || (path.size() >= 8 && path.compare(path.size() - 8, 8, "zero.elf") == 0);
            three = three || (path.size() >= 9 && path.compare(path.size() - 9, 9, "three.elf") == 0);
            dangling = dangling || path.find("dangling.elf") != std::string::npos;
        }
    CHECK(zero && three, "ELF: leere und winzige Datei sind Befunde (too_small)");
    if (!ec) CHECK(!dangling, "ELF: haengender Link (Lesefehler) ist kein Befund");

    // Check-up-Datei mit falschen Feldtypen verwirft nicht die ganze Liste
    Env e2;
    e2.put("/data/ps5medic/checks/20261101T000000Z.json", R"({"version":1,"ts":"20261101T000000Z","findings":[{"rule_id":"a.b","seen_runs":"x","confirm_runs":1e30}]})");
    e2.put("/data/ps5medic/checks/20261102T000000Z.json", R"({"version":1,"ts":"20261102T000000Z","findings":[]})");
    CHECK(list_checks(e2.p->fs()).size() == 2, "Check-up-Liste: Datei mit falschen Typen bleibt gelistet, die andere auch");
}

void redact_tests() {
    const std::string a = anonymize("smb connect failed host=nas user=bob password=hunter2 share=x");
    CHECK(a.find("bob") == std::string::npos && a.find("hunter2") == std::string::npos && a.find("host=nas") != std::string::npos, "Bericht: user=/password= geschwaerzt, Rest bleibt: " + a);
    const std::string b = anonymize("smb://bob:topsecret@192.168.1.5/share ok");
    CHECK(b.find("bob") == std::string::npos && b.find("topsecret") == std::string::npos && b.find("192.168.1.5") == std::string::npos, "Bericht: Zugangsdaten und IP in der Adresse geschwaerzt: " + b);
    const std::string c = anonymize("{\"password\": \"hunter2\", \"x\": 1}");
    CHECK(c.find("hunter2") == std::string::npos && c.find("\"x\": 1") != std::string::npos, "Bericht: JSON-Passwort geschwaerzt: " + c);
    const std::string d = anonymize("passed 3 of 4 tests, user_id: 42");
    CHECK(d.find("passed 3 of 4 tests") != std::string::npos, "Bericht: 'passed' ist kein Schluessel");
    CHECK(redact_credentials("mount ok user=bob pass=hunter2 share=x").find("hunter2") == std::string::npos, "Export: zweites Schluessel=Wert-Paar in einer Zeile geschwaerzt");
    CHECK(anonymize("{\"password\": \"hun\\\"ter2\"}").find("ter2") == std::string::npos, "Bericht: maskiertes Anfuehrungszeichen im JSON-Wert beendet ihn nicht");
    CHECK(anonymize("mypassword=abc userPassword=def").find("abc") == std::string::npos && anonymize("userPassword=def").find("def") == std::string::npos, "Bericht: Schluessel mit Praefix geschwaerzt");
    CHECK(anonymize("Tests: pass: 12").find("pass: 12") != std::string::npos && anonymize("Error: user: not found").find("not found") != std::string::npos &&
              anonymize("pwd=/data/homebrew").find("/data/homebrew") != std::string::npos, "Bericht: harmlose Zeilen bleiben lesbar");
    CHECK(anonymize("http://example.com?mail=a@b.c").find("example.com") != std::string::npos, "Bericht: Host in URL mit ? bleibt");
}

void games_tests() {
    // Ein Befund fuer /data/homebrew/Game2/... gehoert nicht auch zum Spiel in /data/homebrew/Game
    Facts facts;
    facts.set("smp.titles", json::array({json{{"titleid", "PPSA00001"}, {"path", "/data/homebrew/Game"}, {"root", "/data/homebrew"}, {"level", 1}},
                                         json{{"titleid", "PPSA00002"}, {"path", "/data/homebrew/Game2"}, {"root", "/data/homebrew"}, {"level", 1}}}));
    Finding f;
    f.rule_id = "t.rule";
    f.severity = Severity::Warning;
    f.evidence = {"/data/homebrew/Game2/sce_sys/param.json"};
    const auto games = collect_game_infos(facts, {f});
    int with = 0, with1 = 0;
    for (const auto& g : games) {
        if (!g.findings.empty()) ++with;
        if (g.titleid == "PPSA00001" && !g.findings.empty()) ++with1;
    }
    CHECK(with == 1 && with1 == 0, "Spiele: Befund geht nur an Game2, nicht an Game");
    Finding f3;
    f3.rule_id = "t.rule3";
    f3.severity = Severity::Warning;
    f3.evidence = {"/data/homebrew/Game (1)/sce_sys/param.json"};
    int hit3 = 0;
    for (const auto& g : collect_game_infos(facts, {f3}))
        if (g.titleid == "PPSA00001" && !g.findings.empty()) ++hit3;
    CHECK(hit3 == 0, "Spiele: Befund fuer 'Game (1)' gehoert nicht zu 'Game'");

    // Registry-Eintraege ohne eigenen Titel fuellen die Liste nicht
    Facts f2;
    json entries = json::array();
    for (int i = 0; i < 300; ++i) entries.push_back(json{{"titleid", "NPXS" + std::to_string(20000 + i)}, {"path", "/system_ex/app/x/"}});
    f2.set("appdb.entries", entries);
    f2.set("smp.titles", json::array({json{{"titleid", "PPSA00003"}, {"path", "/data/homebrew/G3"}, {"root", "/data/homebrew"}, {"level", 1}}}));
    const auto g2 = collect_game_infos(f2, {});
    CHECK(g2.size() == 1, "Spiele: 300 Registry-Zeilen machen keine 300 'Spiele'");
}

void rules_tests() {
    const std::string base = R"({"schema_version":1,"db_version":"t","rules":[{"id":"t.fw","severity":"info","applies_fw":">= 13.60","when":{"fact":"x","eq":"1"},)"
                             R"("text":{"de":"d","en":"e"},"source":"https://example.org","verified":"doc"}]})";
    const RuleSet rs = load_rules_from_string(base);
    Facts hi, lo;
    hi.set("x", "1");
    hi.set("fw.version", "13.60");
    lo.set("x", "1");
    lo.set("fw.version", "12.00");
    CHECK(evaluate(rs, hi).size() == 1, "applies_fw mit Leerzeichen nach dem Operator: gilt ab 13.60");
    CHECK(evaluate(rs, lo).empty(), "applies_fw mit Leerzeichen nach dem Operator: gilt nicht darunter");

    bool rejected = false;
    try {
        load_rules_from_string(R"({"schema_version":1,"db_version":"t","rules":[{"id":"t.two","severity":"info","when":{"fact":"a[].b[].c","eq":"1"},)"
                               R"("text":{"de":"d","en":"e"},"source":"https://example.org","verified":"doc"}]})");
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected, "Regel mit zwei [] im Fakt-Pfad wird abgelehnt");
}

}  // namespace

void review2_tests() {
    planner_tests();
    collector_tests();
    redact_tests();
    games_tests();
    rules_tests();
}
