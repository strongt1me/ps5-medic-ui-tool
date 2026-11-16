// SPDX-License-Identifier: GPL-3.0-or-later
// Minimaler Test-Runner ohne Fremdabhaengigkeiten.
//  1. Unit-Tests (Versionsvergleich, Operatoren, Schema-Pruefung, Pfadschutz)
//  2. Fixture-Tests: jeder Ordner in tests/fixtures/ wird mit knowledge/rules.json geprueft;
//     expect.json {"expect":["regel.id", ...]} muss exakt der Menge der Befunde entsprechen.
#include "medic/sqlite_check.hpp"
#include "medic/collectors.hpp"
#include "medic/exfat.hpp"
#include "medic/image_reader.hpp"
#include "medic/platform.hpp"
#include "medic/rules.hpp"
#include "check.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
    CHECK(compare_versions("1.7beta3", "1.7") < 0, "Vorabversion ist kleiner als die fertige");
    CHECK(compare_versions("1.7beta9", "1.7beta10") < 0 && compare_versions("1.7beta11", "1.7beta10") > 0, "beta-Nummern zaehlen numerisch");
    CHECK(compare_versions("1.7beta12", "1.7beta12") == 0 && compare_versions("1.7beta1", "1.7alpha12") > 0, "alpha < beta, gleiche Nummer gleich");
    CHECK(compare_versions("1.6test15", "1.7alpha1") < 0 && compare_versions("1.7rc1", "1.7beta12") > 0, "test < alpha < beta < rc, kleinere Hauptversion zuerst");
    CHECK(compare_versions("1.13-fpkg-dr-test8", "1.13") == 0, "unbekannter Suffix wird ignoriert");

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
    {  // G1: verlangte Firmware aus requiredSystemSoftwareVersion der param.json unter /user/app
        auto f = facts_of("g1_pos");
        const json* g = f.get("games.fw");
        CHECK(g && g->is_array() && g->size() == 3, "G1: drei PS5-Titel mit gueltiger Version (Homebrew mit 0 und FAKE-Ordner zaehlen nicht)");
        if (g && g->is_array() && g->size() == 3) {
            CHECK((*g)[0]["titleid"] == "PPSA00001" && (*g)[0]["required_fw"] == "13.60" && (*g)[0]["sdk_fw"] == "12.00" && (*g)[0]["backport"] == false,
                  "G1: 0x1360000000000000 ist Firmware 13.60, sdkVersion 12.00");
            CHECK((*g)[1]["required_fw"] == "11.60" && (*g)[2]["required_fw"] == "12.00", "G1: 11.60 und 12.00");
        }
        const json* n = f.get("games.need_backport");
        CHECK(n && n->is_array() && n->size() == 1 && (*n)[0]["titleid"] == "PPSA00001" && (*n)[0]["desc"] == "PPSA00001: 13.60",
              "G1: nur der Titel mit 13.60 auf einer 12.00-Konsole braucht einen Backport (gleich und aelter zaehlen nicht)");
        auto b = facts_of("g1_neg_backport");
        const json* bg = b.get("games.fw");
        CHECK(bg && bg->size() == 1 && (*bg)[0]["backport"] == true && b.get("games.need_backport")->empty(), "G1: ein Ordner backports/<TITLE_ID>/ im Scanpfad zaehlt als Backport");
        {  // Backport im Spiel selbst: SDK-Version der eboot.bin (Prozessparameter) nicht ueber der Firmware, param.json verlangt mehr
            auto inl = facts_of("g1_neg_inline");
            const json* ig = inl.get("games.fw");
            CHECK(ig && ig->size() == 1 && (*ig)[0]["eboot_sdk"] == "12.00" && (*ig)[0]["backport"] == true && (*ig)[0]["backport_inline"] == true && inl.get("games.need_backport")->empty(),
                  "G1: eboot.bin mit SDK 12.00 bei verlangter 13.60 auf 12.00 gilt als eingebauter Backport");
            auto neu = facts_of("g1_pos_ebootnew");
            const json* ng = neu.get("games.fw");
            CHECK(ng && ng->size() == 1 && (*ng)[0]["eboot_sdk"] == "13.60" && (*ng)[0]["backport"] == false && neu.get("games.need_backport")->size() == 1,
                  "G1: eboot.bin mit SDK 13.60 ist kein Backport, der Befund bleibt");
            // Dasselbe, wenn das Spiel ein exFAT-Image ist: die eboot.bin wird ohne Einhaengen aus dem Image gelesen (zerstueckelte Kette, Name "EBOOT.BIN")
            auto img = facts_of("g1_neg_inline_image");
            const json* mg = img.get("games.fw");
            CHECK(mg && mg->size() == 1 && (*mg)[0]["eboot_sdk"] == "12.00" && (*mg)[0]["backport_inline"] == true && img.get("games.need_backport")->empty(),
                  "G1: eboot.bin im exFAT-Image mit SDK 12.00 gilt als eingebauter Backport");
            for (const char* fixture : {"g1_neg_inline_ffpfs", "g1_neg_inline_ffpfsc"}) {  // PFS-Behaelter mit dem exFAT-Image (unkomprimiert, PFSC-komprimiert)
                auto pf = facts_of(fixture);
                const json* pg = pf.get("games.fw");
                CHECK(pg && pg->size() == 1 && (*pg)[0]["eboot_sdk"] == "12.00" && (*pg)[0]["backport_inline"] == true && pf.get("games.need_backport")->empty(),
                      std::string("G1: eboot.bin im Image-Behaelter (") + fixture + ") mit SDK 12.00 gilt als eingebauter Backport");
            }
            auto imgnew = facts_of("g1_pos_image_ebootnew");
            const json* ig2 = imgnew.get("games.fw");
            CHECK(ig2 && ig2->size() == 1 && (*ig2)[0]["eboot_sdk"] == "13.60" && imgnew.get("games.need_backport")->size() == 1, "G1: Image mit SDK 13.60: der Befund bleibt");
            CHECK((*facts_of("g1_neg_backport").get("games.fw"))[0]["eboot_sdk"] == "" && (*facts_of("g1_neg_backport").get("games.fw"))[0]["backport_inline"] == false, "G1: ohne lesbare eboot.bin bleibt die SDK-Version leer");
        }
        CHECK(facts_of("g1_neg_nofw").get("games.need_backport")->empty() && facts_of("g1_neg_nofw").get("games.fw")->size() == 1, "G1: ohne bekannte Firmware kein Befund, die Liste bleibt");
        CHECK(facts_of("g1_neg_badformat").get("games.fw")->empty(), "G1: ungueltige Versionsangaben (zu kurz, keine BCD-Ziffern, Klartext) werden ignoriert");
        CHECK(facts_of("a3_neg").get("games.fw")->empty(), "G1: ohne /user/app leere Liste");
    }
    {  // C2: nur Apps mit grosser Reservierung und kleinem Inhalt
        auto f = facts_of("c2_pos");
        const json* v = f.get("apps.oversized");
        CHECK(v && v->is_array() && v->size() == 1, "C2: ein verdaechtige App");
        if (v && v->is_array() && v->size() == 1) {
            const json& a = (*v)[0];
            // eboot.bin (2048) + icon0.png (512) + param.json (ein paar hundert Byte)
            CHECK(a["titleid"] == "PPSA00099" && a["name"] == "Test App" && a["reserved_mib"] == 16384 && a["content_bytes"] > 2560 &&
                      a["content_bytes"] < 3584,
                  "C2: Titel, Name, Reservierung und Inhaltsgroesse");
        }
        auto neg = facts_of("c2_neg");
        const json* none = neg.get("apps.oversized");
        CHECK(none && none->is_array() && none->empty(), "C2: 256 MiB sind unauffaellig");
    }
    {  // SM+-Version aus dem Log-Anfang, wenn das Log laenger ist als die gelesenen letzten Zeilen (Konsole nach Stunden)
        auto f = facts_of("b3_pos_longlog");
        const json* lines = f.get("smp.log.lines");
        bool banner_in_tail = false;
        if (lines && lines->is_array())
            for (auto& l : *lines) banner_in_tail = banner_in_tail || (l.is_string() && l.get<std::string>().find("ShadowMount+ v") != std::string::npos);
        CHECK(lines && lines->is_array() && lines->size() == 2000 && !banner_in_tail, "SM+-Version: im gelesenen Log-Ende steht die Startzeile nicht mehr");
        CHECK(str(f, "smp.version") == "1.7beta3", "SM+-Version: kommt aus dem Anfang des Logs");
    }
    {  // Konfigurationsformat v2 (SM+ 1.7beta12): wie SM+ lesen (Sektionen, on/off, Anfuehrungszeichen, Kommentare hinter Werten)
        auto f = facts_of("b13_pos_v2");
        const json* roots = f.get("smp.scan_roots");
        CHECK(roots && roots->is_array() && roots->size() == 2 && (*roots)[0] == "/mnt/usb0/Meine Spiele" && (*roots)[1] == "/data/homebrew",
              "Config v2: Pfad in Anfuehrungszeichen (mit Leerzeichen) und Pfad mit Kommentar dahinter");
        CHECK(num(f, "smp.debug") == 0 && num(f, "smp.scan_depth") == 2 && num(f, "smp.fakelib_enabled") == 0, "Config v2: debug = off, depth = 2 ; Kommentar, enabled = no");
        auto d = facts_of("b13_neg_v2_defaults");
        CHECK(num(d, "smp.debug") == 1 && num(d, "smp.scan_depth") == 1 && num(d, "smp.fakelib_enabled") == 1, "Config v2: auskommentierte Standardwerte bleiben Standard");
        auto v1 = facts_of("b13_pos_v1_comment");
        CHECK(num(v1, "smp.debug") == 0 && num(v1, "smp.scan_depth") == 2, "Config v1: Kommentar hinter dem Wert (wie SM+: # und ; leiten ihn ein)");
    }
    {  // B19: API-Port aus der Bereit-Zeile von ShadowMount+
        auto f = facts_of("b19_pos");
        CHECK(str(f, "smp.api.bind") == "0.0.0.0" && str(f, "smp.api.port") == "10101" && str(f, "smp.api.state") == "closed", "B19: Adresse, Port und Zustand aus Log und Plattform");
        CHECK(str(facts_of("b19_neg_open"), "smp.api.state") == "open", "B19: offener Port");
        auto lan = facts_of("b19_neg_lan_bind");
        CHECK(str(lan, "smp.api.bind") == "192.168.1.50" && str(lan, "smp.api.state") == "<fehlt>", "B19: an eine LAN-Adresse gebunden: Loopback-Versuch sagt nichts, Zustand bleibt unbekannt");
        auto none = facts_of("b19_neg_no_ready_line");
        CHECK(str(none, "smp.api.port") == "<fehlt>" && str(none, "smp.api.state") == "<fehlt>", "B19: ohne Bereit-Zeile gar keine Fakten");
        auto lg = facts_of("b19_pos_longlog");
        const json* lines = lg.get("smp.log.lines");
        bool ready_in_tail = false;
        if (lines && lines->is_array())
            for (auto& l : *lines) ready_in_tail = ready_in_tail || (l.is_string() && l.get<std::string>().find("HTTP/JSON ready") != std::string::npos);
        CHECK(lines && lines->size() == 2000 && !ready_in_tail && str(lg, "smp.api.state") == "closed", "B19: Bereit-Zeile steht nur am Anfang eines langen Logs und wird trotzdem gefunden");
    }
    {  // B11: Zeilen werden je Quelle gezaehlt, nicht insgesamt
        auto count_of = [&](const char* fixture, const std::string& path) -> long {
            auto f = facts_of(fixture);
            const json* a = f.get("smp.unstable_sources");
            if (!a || !a->is_array()) return -1;
            for (auto& e : *a)
                if (e["path"] == path) return e["count"].get<long>();
            return 0;
        };
        CHECK(count_of("b11_pos", "/data/homebrew/Big.ffpfs") == 3, "B11: dreimal dieselbe Quelle, nur der Pfad in der Zeile");
        CHECK(count_of("b11_pos_named", "/mnt/usb0/homebrew/Big Game (5).ffpfs") == 3 && count_of("b11_pos_named", "/data/homebrew/PPSA99001") == 1,
              "B11: Name und Pfad mit Klammern, die andere Quelle zaehlt extra");
        CHECK(count_of("b11_neg_distinct_sources", "/data/homebrew/PPSA99210") == 2 && count_of("b11_neg_distinct_sources", "/data/homebrew/FAKE42007") == 1 &&
                  count_of("b11_neg_distinct_sources", "/data/homebrew/Zweites Beispiel-lz4") == 1,
              "B11: die Zeilen des echten Logs verteilen sich auf drei Quellen");
        CHECK(count_of("a3_neg", "x") == 0, "B11: ohne solche Zeilen eine leere Liste");
    }
    {  // Spielstaende: Konto-ID der Metadaten gegen die des Spielstand-Speichers (sce_sdmemory = jetziges Konto)
        const auto pos = facts_of("d2_pos");
        const json* fg = pos.get("savedata.foreign");
        CHECK(str(pos, "savedata.current_account") == "0200000000000000", "Spielstand: aktuelle Konto-ID aus dem Spielstand-Speicher");
        CHECK(fg && fg->is_array() && fg->size() == 2 && (*fg)[0]["titleid"] == "PPSA77300" && (*fg)[0]["name"] == "SaveSlot1" && (*fg)[0]["account"] == "0100000000000000" &&
                  (*fg)[0]["label"] == "PPSA77300 SaveSlot1 (Testspiel)" && (*fg)[1]["titleid"] == "PPSA77301",
              "Spielstand: zwei Spielstaende mit anderer Konto-ID, der Speicher und der Spielstand mit passender ID zaehlen nicht");
        auto count_of = [&](const char* fixture) {
            const auto f = facts_of(fixture);
            const json* a2 = f.get("savedata.foreign");
            return a2 && a2->is_array() ? static_cast<long>(a2->size()) : -1L;
        };
        CHECK(count_of("d2_neg_same_account") == 0 && str(facts_of("d2_neg_same_account"), "savedata.current_account") == "0200000000000000", "Spielstand: gleiche Konto-ID -> nichts");
        CHECK(count_of("d2_neg_no_current") == 0 && !facts_of("d2_neg_no_current").get("savedata.current_account"), "Spielstand: ohne Spielstand-Speicher ist das jetzige Konto unbekannt -> nichts");
        CHECK(count_of("d2_neg_two_currents") == 0, "Spielstand: zwei verschiedene Konto-IDs in den Speichern -> unklar, nichts melden");
        CHECK(count_of("a3_neg") == 0, "Spielstand: ohne Metadaten eine leere Liste");
    }
    {  // Startverlauf (vshlog): Boot up direkt nach Boot up = Sitzung ohne protokolliertes Herunterfahren
        const auto pos = facts_of("e2_pos");
        CHECK(num(pos, "boot.unclean_total") == 3 && num(pos, "boot.unclean_14d") == 3, "Startverlauf: drei Neustarts ohne Herunterfahren, alle in 14 Tagen");
        CHECK(str(pos, "boot.log_last") == "2026-11-10T08:19:27Z", "Startverlauf: letzter Eintrag als Zeitbezug");
        const json* u = pos.get("boot.unclean");
        CHECK(u && u->is_array() && u->size() == 3 && (*u)[2]["ts"] == "2026-11-09T08:19:27Z" && (*u)[2]["cause"] == "MAIN_SOC", "Startverlauf: Zeitpunkt und Startursache");
        const auto info = facts_of("e2_info");
        CHECK(num(info, "boot.unclean_total") == 2 && num(info, "boot.unclean_14d") == 1, "Startverlauf: ein alter und ein junger Eintrag -> nur einer im Fenster");
        CHECK(num(facts_of("e2_neg_clean"), "boot.unclean_total") == 0 && num(facts_of("e2_neg_clean"), "boot.boots_total") == 3, "Startverlauf: normale Neustarts zaehlen nicht");
        CHECK(!facts_of("a3_neg").get("boot.log_last"), "Startverlauf: ohne Datei kein Fakt");
    }
    {  // Fehlerverlauf: Paare aus Code und Titel
        const auto pos = facts_of("e1_pos");
        const json* pr = pos.get("errors.pairs");
        long ce = -1, wv = -1;
        if (pr && pr->is_array())
            for (const auto& e : *pr) {
                if (e["code"] == "CE-100001-1" && e["titleid"] == "PPSA77401") ce = e["count"].get<long>();
                if (e["code"] == "WV-100002-0") wv = e["count"].get<long>();
            }
        CHECK(ce == 3 && wv == 6 && num(pos, "errors.total") == 9, "Fehlerverlauf: Paare gezaehlt (auch mit BOM am Dateianfang)");
        CHECK(pr && (*pr)[0]["label"] == "CE-100001-1 - PPSA77401 - 3x, 2026-11-07", "Fehlerverlauf: Beschriftung mit Code, Titel, Anzahl und letztem Tag");
        CHECK(facts_of("a3_neg").get("errors.total") == nullptr, "Fehlerverlauf: ohne Ordner fehlt der Fakt (nicht geprueft), keine Summe 0");
    }
    {  // Datenbanken (SQLite): Kopf, Groesse, Journal
        auto dbf = [&](const char* fixture, const char* name) -> json {
            const auto f = facts_of(fixture);
            const json* a = f.get("db.files");
            if (a && a->is_array())
                for (const auto& e : *a)
                    if (e["name"] == name) return e;
            return json();
        };
        const json ok = dbf("db1_neg_ok", "app.db");
        CHECK(ok.is_object() && ok["readable"] == 1 && ok["header_bad"] == 0 && ok["truncated"] == 0 && ok["page_size"] == 512 && ok["header_pages"] == 11 && ok["journal_size"] == -1,
              "Datenbank: gesunde Datei, Seitengroesse und Seitenzahl aus dem Kopf");
        CHECK(num(facts_of("db1_neg_ok"), "db.count") == 2, "Datenbank: beide Dateien gefunden");
        CHECK(dbf("db1_pos_header", "app.db")["header_bad"] == 1, "Datenbank: falsche Kennung -> Kopf ungueltig");
        CHECK(dbf("db1_pos_pagesize", "app.db")["header_bad"] == 1, "Datenbank: Seitengroesse 777 ist keine Zweierpotenz -> Kopf ungueltig");
        CHECK(dbf("db1_pos_truncated", "app.db")["truncated"] == 1 && dbf("db1_pos_truncated", "app.db")["size"] == 2048, "Datenbank: Kopf sagt 10 Seiten, Datei hat 4 -> abgeschnitten");
        CHECK(dbf("db1_neg_counter_mismatch", "app.db")["truncated"] == 0, "Datenbank: Seitenzahl im Kopf gilt nicht, wenn der Aenderungszaehler nicht passt");
        CHECK(dbf("db1_pos_journal", "app.db")["journal_hot"] == 1 && dbf("db1_pos_journal", "app.db")["journal_size"] == 512, "Datenbank: Journal mit Kennung -> offene Transaktion");
        CHECK(dbf("db1_neg_journal_cold", "app.db")["journal_hot"] == 0 && dbf("db1_neg_journal_cold", "app.db")["journal_size"] == 512, "Datenbank: Journal mit Nullen ist keine offene Transaktion");
        CHECK(num(facts_of("a3_neg"), "db.count") == 0, "Datenbank: ohne Dateien Summe 0");
    }
    {  // Datenbanken, Stufe 2: integrity_check im Arbeitsspeicher
        auto dbf = [&](const char* fixture, const char* name) -> json {
            const auto f = facts_of(fixture);
            const json* a = f.get("db.files");
            if (a && a->is_array())
                for (const auto& e : *a)
                    if (e["name"] == name) return e;
            return json();
        };
        CHECK(dbf("db2_neg_ok", "app.db")["integrity"] == 1 && dbf("db2_neg_ok", "app.db")["integrity_note"] == "", "Datenbank: gesunde echte SQLite-Datei besteht integrity_check");
        const json bad = dbf("db2_pos_corrupt", "app.db");
        CHECK(bad["integrity"] == 0 && !bad["integrity_note"].get<std::string>().empty() && bad["header_bad"] == 0 && bad["truncated"] == 0,
              "Datenbank: Seite 2 zerstoert -> Kopf ok, integrity_check schlaegt fehl");
        CHECK(dbf("db2_pos_corrupt", "appinfo.db")["integrity"] == 1, "Datenbank: die zweite, gesunde Datei bleibt unauffaellig");
        CHECK(dbf("db2_wal_ok", "app.db")["integrity"] == 1 && dbf("db2_wal_ok", "app.db")["wal_size"] > 32 && dbf("db2_wal_ok", "app.db")["integrity_skip"] == "",
              "Datenbank: Hauptdatei plus WAL mit Eintraegen -> WAL wird eingerechnet, Pruefung besteht");
        CHECK(dbf("db2_wal_main_corrupt", "app.db")["integrity"] == 0, "Datenbank: zerstoerte Seite, die das WAL nicht ueberschreibt -> Fehler trotz WAL");
        CHECK(dbf("db2_wal_garbage", "app.db")["integrity"] == 1, "Datenbank: WAL mit ungueltigem Kopf wird wie bei SQLite ignoriert");
        CHECK(dbf("db1_pos_truncated", "app.db")["integrity_skip"] == "kopf" && dbf("db1_pos_journal", "app.db")["integrity_skip"] == "journal" &&
                  dbf("db1_pos_header", "app.db")["integrity_skip"] == "kopf" && dbf("db1_neg_ok", "app.db")["integrity_skip"] == "",
              "Datenbank: der Grund fuer eine ausgelassene Pruefung steht im Fakt integrity_skip");
        CHECK(dbf("db2_neg_hot_journal", "app.db")["integrity"] == -1 && dbf("db2_neg_hot_journal", "app.db")["journal_hot"] == 1, "Datenbank: mit offenem Journal wird nicht geprueft");
        CHECK(dbf("db1_neg_ok", "app.db")["integrity"] == 1 && dbf("db1_pos_truncated", "app.db")["integrity"] == -1, "Datenbank: abgeschnitten -> nicht geprueft, gesund -> geprueft");
        const std::string sample = medic::sqlite_broken_sample();
        CHECK(sample.size() >= 1536 && !medic::sqlite_integrity_check(sample).ok && medic::sqlite_integrity_check(sample).ran, "Datenbank: das synthetische Beispiel fuer den Export ist beschaedigt");
    }
    {  // Spielordner: Dateiname nur in Gross-/Kleinschreibung falsch (UFS unterscheidet, exFAT nicht)
        auto cm = [&](const char* fixture) -> json {
            const auto f = facts_of(fixture);
            const json* a = f.get("games.case_mismatch");
            return a && a->is_array() && a->size() == 1 ? (*a)[0] : json();
        };
        const json pos = cm("gc1_pos");
        CHECK(pos.is_object() && pos["titleid"] == "PPSA77500" && pos["actual"] == "Il2cppUserAssemblies.prx" && pos["expected"] == "Il2CppUserAssemblies.prx" &&
                  pos["case_sensitive"] == 1 && pos["path"] == "/data/homebrew/PPSA77500/Media/Modules/Il2cppUserAssemblies.prx",
              "Spielordner: Il2cppUserAssemblies.prx auf nullfs -> Dateiname weicht ab, Dateisystem unterscheidet");
        const json ex = cm("gc1_neg_exfat");
        CHECK(ex.is_object() && ex["fstype"] == "exfatfs" && ex["case_sensitive"] == 0, "Spielordner: auf exFAT harmlos (case_sensitive 0)");
        const auto good = facts_of("gc1_neg_correct");
        CHECK(good.get("games.case_mismatch") && good.get("games.case_mismatch")->empty(), "Spielordner: richtiger Name -> nichts");
        CHECK(facts_of("a3_neg").get("games.case_mismatch") && facts_of("a3_neg").get("games.case_mismatch")->empty(), "Spielordner: ohne Spiele eine leere Liste");
    }
    {  // Abdeckung: "nicht geprueft" statt "in Ordnung" (Bereiche stehen in knowledge/rules.json)
        const auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        CHECK(rules.coverage.size() == 14 && rules.coverage[0].id == "processes" && rules.coverage[11].id == "klog" && rules.coverage[12].id == "game_runtime" && rules.coverage.back().id == "app_registry", "Abdeckung: 14 Bereiche aus der Regeldatei (zuletzt Kernel-Log und Titelregister)");
        auto cov = [&](const char* fixture) {
            std::map<std::string, bool> m;
            for (const auto& c : medic::evaluate_coverage(rules, facts_of(fixture))) m[c.id] = c.tested;
            return m;
        };
        const auto boot = cov("e2_pos");
        CHECK(boot.at("boot_log") && !boot.at("error_history") && !boot.at("databases_header") && !boot.at("databases_content"), "Abdeckung: nur der Startverlauf gelesen");
        const auto errs = cov("e1_pos");
        CHECK(errs.at("error_history") && !errs.at("boot_log"), "Abdeckung: Fehlerverlauf gelesen, Startverlauf nicht");
        const auto db = cov("db2_neg_ok");
        CHECK(db.at("databases_header") && db.at("databases_content"), "Abdeckung: Datenbanken gelesen und im Inhalt geprueft");
        const auto trunc = cov("db1_pos_truncated");
        CHECK(trunc.at("databases_header") && !trunc.at("databases_content"), "Abdeckung: abgeschnittene Datenbank -> Inhalt nicht geprueft");
        const auto acc = cov("d2_pos");
        CHECK(acc.at("savedata_account") && !cov("a3_neg").at("savedata_account"), "Abdeckung: Konto der Spielstaende nur mit Spielstand-Speicher bekannt");
        auto throws = [](const std::string& coverage) {
            try {
                medic::load_rules_from_string("{\"schema_version\":1,\"db_version\":\"t\",\"rules\":[],\"coverage\":" + coverage + "}");
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        const std::string ok_item = R"({"id":"a","text":{"de":"A","en":"A"},"tested_when":{"all":[{"fact":"x","exists":true}]}})";
        CHECK(!throws("[]") && !throws("[" + ok_item + "]"), "Abdeckung: gueltige Listen werden geladen");
        CHECK(throws("{}") && throws("[1]") && throws("[" + ok_item + "," + ok_item + "]"), "Abdeckung: kein Array, kein Objekt, doppelte ID -> Fehler");
        CHECK(throws(R"([{"id":"a","text":{"de":"A","en":"A"}}])") && throws(R"([{"id":"A B","text":{"de":"A","en":"A"},"tested_when":{"all":[{"fact":"x","exists":true}]}}])") &&
                  throws(R"([{"id":"a","text":{"de":"A"},"tested_when":{"all":[{"fact":"x","exists":true}]}}])") && throws(R"([{"id":"a","text":{"de":"A","en":"A"},"tested_when":{"bogus":1}}])"),
              "Abdeckung: fehlendes tested_when, ungueltige ID, fehlender Text, ungueltige Bedingung -> Fehler");
        CHECK(medic::load_rules_from_string("{\"schema_version\":1,\"db_version\":\"t\",\"rules\":[]}").coverage.empty(), "Abdeckung: Liste ist optional");
    }
    {  // Messwerte und confirm_runs
        auto throws = [](const std::string& extra, const std::string& rule_extra = "") {
            try {
                medic::load_rules_from_string("{\"schema_version\":1,\"db_version\":\"t\",\"rules\":[" +
                                              (rule_extra.empty() ? std::string() : "{\"id\":\"x.y\",\"severity\":\"info\",\"when\":{\"all\":[{\"fact\":\"a\",\"exists\":true}]},\"text\":{\"de\":\"d\",\"en\":\"e\"},\"source\":\"https://example.org\",\"verified\":\"doc\"," + rule_extra + "}") +
                                              "]" + extra + "}");
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        const std::string ok_m = R"({"id":"a","fact":"sys.temp.cpu_c","unit":"C","text":{"de":"A","en":"A"}})";
        CHECK(!throws(",\"metrics\":[]") && !throws(",\"metrics\":[" + ok_m + "]"), "Messwerte: gueltige Listen laden");
        CHECK(throws(",\"metrics\":{}") && throws(",\"metrics\":[1]") && throws(",\"metrics\":[" + ok_m + "," + ok_m + "]") &&
                  throws(",\"metrics\":[{\"id\":\"A B\",\"fact\":\"x\",\"text\":{\"de\":\"A\",\"en\":\"A\"}}]") && throws(",\"metrics\":[{\"id\":\"a\",\"fact\":\"x[].y\",\"text\":{\"de\":\"A\",\"en\":\"A\"}}]") &&
                  throws(",\"metrics\":[{\"id\":\"a\",\"fact\":\"x\",\"text\":{\"de\":\"A\"}}]") && throws(",\"metrics\":[{\"id\":\"a\",\"text\":{\"de\":\"A\",\"en\":\"A\"}}]"),
              "Messwerte: kein Array, kein Objekt, doppelte ID, ungueltige ID, [] im Pfad, fehlender Text oder Fakt -> Fehler");
        CHECK(!throws("", "\"confirm_runs\":2") && throws("", "\"confirm_runs\":0") && throws("", "\"confirm_runs\":11") && throws("", "\"confirm_runs\":\"2\""), "confirm_runs: 1 bis 10, sonst Fehler");
        const auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        CHECK(rules.metrics.size() == 5 && rules.metrics[0].id == "cpu_temp", "Messwerte: fuenf Messwerte aus der Regeldatei");
        const auto hot = medic::evaluate_metrics(rules, facts_of("sys_pos_temp"));
        std::map<std::string, double> got(hot.begin(), hot.end());
        CHECK(got.at("cpu_temp") == 91 && got.at("soc_temp") == 88 && got.at("data_free_pct") > 44.4 && got.at("data_free_pct") < 44.5 && !got.count("unclean_boots_14d"),
              "Messwerte: Werte aus den Fakten (fehlende fehlen)");
        const auto fnd = medic::evaluate(rules, facts_of("sys_pos_temp"));
        const auto it = std::find_if(fnd.begin(), fnd.end(), [](const medic::Finding& f) { return f.rule_id == "sys.temp_high"; });
        CHECK(it != fnd.end() && it->confirm_runs == 2 && medic::is_pending(*it), "confirm_runs: sys.temp_high ist eine Momentaufnahme und zunaechst vorlaeufig");
    }
    {  // Kernel-Log (klog): gefilterte Zeilen, ohne eigene Ausgabe und Doppelte; ohne Port keine Fakten und "nicht geprueft"
        const auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        const auto f = facts_of("k1_pos_kstuff");
        const json* lines = f.get("klog.lines");
        CHECK(f.get("klog.readable") && lines && lines->is_array() && f.get("klog.total_lines")->get<long>() == 6, "klog: lesbar, sechs Zeilen gezaehlt");
        bool own = false, prefix = false, noise = false, kept = false;
        for (const auto& l : *lines) {
            const std::string t = l.get<std::string>();
            own = own || t.find("[payload.elf]") != std::string::npos;
            prefix = prefix || t.rfind("<118>", 0) == 0;
            noise = noise || t.find("GetExecutable") != std::string::npos;
            kept = kept || t.find("patch_shellui failed") != std::string::npos;
        }
        CHECK(!own && !prefix && !noise && kept, "klog: Prioritaetsvorsatz entfernt, eigene Ausgabe und Unauffaelliges nicht uebernommen, Fehlerzeile behalten");
        const auto none = facts_of("a3_neg");
        CHECK(!none.get("klog.readable") && !none.get("klog.lines"), "klog: ohne erreichbaren Port keine Fakten");
        auto cov = [&](const medic::Facts& x) {
            for (const auto& c : medic::evaluate_coverage(rules, x))
                if (c.id == "klog") return c.tested;
            return false;
        };
        CHECK(cov(f) && !cov(none), "klog: Abdeckung 'Kernel-Log' nur bei lesbarem Port (sonst nicht geprueft)");
    }
    {  // Titelregister (app.db): nur Titel-ID und Pfad, Abgleich mit den Ordnern; ohne lesbare Datenbank "nicht geprueft"
        const auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        const auto f = facts_of("ap1_pos_orphan");
        CHECK(f.get("appdb.registered")->get<long>() == 2 && f.get("appdb.entries")->size() == 2 && f.get("appdb.orphans")->size() == 1 && (*f.get("appdb.orphans"))[0]["titleid"] == "PPSA00002" && f.get("appdb.unregistered")->empty(),
              "Titelregister: zwei Eintraege, einer ohne Ordner");
        const auto u = facts_of("ap1_pos_unregistered");
        CHECK(u.get("appdb.orphans")->empty() && u.get("appdb.unregistered")->size() == 1 && (*u.get("appdb.unregistered"))[0]["titleid"] == "PPSA00003", "Titelregister: Ordner ohne Eintrag");
        const auto ok = facts_of("ap1_neg_ok");
        CHECK(ok.get("appdb.orphans")->empty() && ok.get("appdb.unregistered")->empty(), "Titelregister: System-Eintraege ohne /user/app-Pfad und NPXS-Ordner sind keine Befunde");
        const auto none = facts_of("a3_neg");
        CHECK(!none.get("appdb.registered"), "Titelregister: ohne app.db keine Fakten");
        auto cov = [&](const medic::Facts& x) {
            for (const auto& c : medic::evaluate_coverage(rules, x))
                if (c.id == "app_registry") return c.tested;
            return false;
        };
        CHECK(cov(f) && !cov(none), "Titelregister: Abdeckung nur bei lesbarer Datenbank");
        CHECK(!facts_of("db1_pos_header").get("appdb.registered"), "Titelregister: eine Datei ohne gueltigen Kopf wird nicht gelesen");
        const std::string reg = medic::sqlite_make_registry({{"PPSA00001", "/user/app/PPSA00001/sce_sys"}});
        const auto rows = medic::sqlite_select_text(reg, "", "SELECT titleId, metaDataPath FROM tbl_contentinfo", 2, 10);
        CHECK(rows.ran && rows.rows.size() == 1 && rows.rows[0][0] == "PPSA00001" && !medic::sqlite_select_text(reg, "", "SELECT x FROM gibt_es_nicht", 1, 10).ran, "Titelregister: Abfrage auf der Arbeitskopie, fehlende Tabelle = nicht ausfuehrbar");
    }
    {  // Formatstufe: ohne Angabe Stufe 1, hoehere Stufen lehnt der Lader ab
        auto with = [](const std::string& level) { return "{\"schema_version\":1,\"db_version\":\"t\",\"rules\":[]" + (level.empty() ? std::string() : ",\"requires_level\":" + level) + "}"; };
        auto throws = [&](const std::string& level) {
            try {
                medic::load_rules_from_string(with(level));
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        CHECK(medic::load_rules_from_string(with("")).requires_level == 1 && medic::load_rules_from_string(with("1")).requires_level == 1, "Formatstufe: ohne Angabe und Stufe 1 laden");
        CHECK(throws(std::to_string(medic::kRuleFormatLevel + 1)) && throws("0") && throws("\"x\"") && throws("1.5"), "Formatstufe: zu hoch, 0, Text und Kommazahl -> Fehler");
        CHECK(medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json").requires_level <= medic::kRuleFormatLevel, "Formatstufe: die mitgelieferte Regeldatei passt zur App");
    }
    {  // Konfidenz mit Grund: Standard aus "verified", Abweichung nur mit Grund, Widersprueche werden abgelehnt
        auto rule_text = [](const std::string& verified, const std::string& conf) {
            return std::string(R"({"schema_version":1,"db_version":"t","rules":[{"id":"x.y","severity":"info","when":{"all":[{"fact":"a","exists":true}]},"text":{"de":"d","en":"e"},"source":"https://example.org","verified":")") +
                   verified + "\"" + (conf.empty() ? "" : ",\"confidence\":" + conf) + "}]}";
        };
        auto load = [&](const std::string& verified, const std::string& conf) { return medic::load_rules_from_string(rule_text(verified, conf)).rules.at(0).confidence; };
        auto throws = [&](const std::string& verified, const std::string& conf) {
            try {
                medic::load_rules_from_string(rule_text(verified, conf));
                return false;
            } catch (const std::exception&) {
                return true;
            }
        };
        const std::string reason = R"("reason":{"de":"Grund","en":"Reason"})";
        const auto doc = load("doc", ""), com = load("community", ""), todo = load("todo", "");
        CHECK(doc.level == "high" && doc.basis == "doc" && com.level == "medium" && com.basis == "community" && todo.level == "low" && todo.basis == "unknown" && doc.reason_de.empty(),
              "Konfidenz: Standard aus verified (doc hoch, community mittel, todo niedrig), ohne Grund");
        const auto obs = load("todo", R"({"level":"medium","basis":"observed",)" + reason + "}");
        CHECK(obs.level == "medium" && obs.basis == "observed" && obs.reason_de == "Grund" && obs.reason_en == "Reason", "Konfidenz: Angabe mit Grund wird uebernommen");
        const auto part = load("doc", R"({"level":"medium",)" + reason + "}");
        CHECK(part.level == "medium" && part.basis == "doc", "Konfidenz: fehlende Felder kommen aus verified");
        CHECK(throws("doc", "1") && throws("doc", R"({"level":"medium","reason":{"de":"G","en":"R"},"x":1})") && throws("doc", R"({"level":"sehr","reason":{"de":"G","en":"R"}})") &&
                  throws("doc", R"({"basis":"magic","reason":{"de":"G","en":"R"}})"),
              "Konfidenz: kein Objekt, unbekannter Schluessel, ungueltige Stufe/Grundlage -> Fehler");
        CHECK(throws("doc", R"({"level":"medium"})") && throws("doc", R"({"level":"medium","reason":{"de":"G"}})") && throws("doc", R"({"level":"medium","reason":{"de":"","en":"R"}})") &&
                  throws("doc", R"({"level":"medium","reason":"G"})"),
              "Konfidenz: fehlender oder leerer Grund -> Fehler");
        CHECK(throws("todo", R"({"basis":"doc",)" + reason + "}") && throws("todo", R"({"basis":"community",)" + reason + "}") && throws("community", R"({"basis":"doc",)" + reason + "}"),
              "Konfidenz: Grundlage staerker als verified -> Fehler");
        CHECK(throws("todo", R"({"level":"high","basis":"observed",)" + reason + "}") && throws("community", R"({"level":"high",)" + reason + "}") && throws("doc", R"({"basis":"derived",)" + reason + "}"),
              "Konfidenz: ohne Beleg nie hoch, hoch nur mit doc/observed -> Fehler");
        CHECK(!throws("doc", R"({"level":"high","basis":"observed",)" + reason + "}") && !throws("doc", R"({"basis":"derived","level":"low",)" + reason + "}"), "Konfidenz: erlaubte Kombinationen werden geladen");
        CHECK(throws("doc", R"({"level":"low","reason":{"de":"Systemsoftware aktualisieren","en":"R"}})"), "Konfidenz: der Grund unterliegt der Textregel (kein Systemupdate)");

        const auto rules = medic::load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
        std::string todo_without_reason, high_without_proof;
        for (const auto& r : rules.rules) {
            if (r.verified == "todo" && (r.confidence.reason_de.empty() || r.confidence.reason_en.empty())) todo_without_reason += r.id + " ";
            if (r.verified == "todo" && r.confidence.level == "high") high_without_proof += r.id + " ";
        }
        CHECK(todo_without_reason.empty(), "Konfidenz: jede Regel ohne Beleg (verified=todo) erklaert ihre Verlaesslichkeit: " + todo_without_reason);
        CHECK(high_without_proof.empty(), "Konfidenz: keine Regel ohne Beleg ist 'hoch': " + high_without_proof);
        auto finding = [&](const char* fixture, const std::string& id) -> const medic::Finding* {
            static std::vector<medic::Finding> keep;
            keep = medic::evaluate(rules, facts_of(fixture));
            for (const auto& f : keep)
                if (f.rule_id == id) return &f;
            return nullptr;
        };
        const auto* acc = finding("d2_pos", "savedata.foreign_account");
        CHECK(acc && acc->confidence.level == "medium" && acc->confidence.basis == "observed" && !acc->confidence.reason_de.empty() && !acc->confidence.reason_en.empty(),
              "Konfidenz: der Befund 'Konto der Spielstaende' traegt Stufe, Grundlage und Grund");
        const auto* db = finding("db2_pos_corrupt", "db.integrity_failed");
        CHECK(db && db->confidence.level == "high" && db->confidence.basis == "doc" && !db->confidence.reason_de.empty(), "Konfidenz: Datenbank-Pruefung hoch/dokumentiert mit Grund");
        medic::Finding bare;
        bare.verified = "community";
        const auto eff = medic::effective_confidence(bare);
        CHECK(eff.level == "medium" && eff.basis == "community", "Konfidenz: Befund ohne Angabe nimmt den Standard aus verified");
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

void image_tests() {
    using namespace medic;
    namespace fs = std::filesystem;
    std::string content(0x1200 + 77, 'x');
    for (std::size_t i = 0; i < content.size(); ++i) content[i] = static_cast<char>('a' + i % 23);
    const fs::path root = fs::temp_directory_path() / ("medic-exfat-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));  // eindeutig: parallele Testlaeufe
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "fs/data");
    const std::pair<unsigned, unsigned> shapes[] = {{9, 0}, {9, 3}, {12, 4}};  // Sektor 512/4096, Cluster 512 B, 4 KB, 64 KB
    for (const auto& [bps, spc] : shapes) {
        const std::string img = build_exfat_stub(bps, spc, "eboot.bin", content);
        const std::string tag = std::to_string(bps) + "/" + std::to_string(spc);
        CHECK(!img.empty(), "exFAT: Ersatz-Image gebaut " + tag);
        {
            std::ofstream out(root / "fs/data/t.exfat", std::ios::binary);
            out.write(img.data(), static_cast<std::streamsize>(img.size()));
        }
        auto p = make_snapshot_platform(root.string());
        const auto image = ImageReader::open(p->fs(), "/data/t.exfat");
        CHECK(image && image->kind() == "exfat", "Image: als exFAT erkannt " + tag);
        if (!image) continue;
        std::string got;
        CHECK(image->read_file("/EBOOT.BIN", 0, content.size(), got) && got == content, "Image: ganze Datei gelesen (Gross-/Kleinschreibung egal) " + tag);
        CHECK(image->read_file("/eboot.bin", 4000, 300, got) && got == content.substr(4000, 300), "Image: Bereich ueber Clustergrenzen " + tag);
        CHECK(!image->read_file("/eboot.bin", 0, content.size() + 1, got) && !image->read_file("/eboot.bin", content.size() + 1, 1, got), "Image: ueber das Dateiende hinaus lesen scheitert " + tag);
        CHECK(!image->read_file("/nicht-da.bin", 0, 1, got), "Image: fehlende Datei " + tag);
    }
    {  // kein exFAT
        std::ofstream out(root / "fs/data/t.exfat", std::ios::binary);
        out << std::string(2048, 'z');
    }
    auto p = make_snapshot_platform(root.string());
    CHECK(!ImageReader::open(p->fs(), "/data/t.exfat") && !ImageReader::open(p->fs(), "/data/gibt-es-nicht.exfat"), "Image: kein bekanntes Format und fehlende Datei werden abgelehnt");
    CHECK(build_exfat_stub(9, 0, "dieser-name-ist-zu-lang.bin", "x").empty() && build_exfat_stub(8, 0, "a", "x").empty(), "exFAT: Bauer lehnt unpassende Angaben ab");
    fs::remove_all(root, ec);
}

int main() {
    unit_tests();
    image_tests();
    platform_tests();
    new_fact_tests();
    fixture_tests();
    fix_tests();
    report_tests();
    update_tests();
    sign_tests();
    error_code_tests();
    codes_update_tests();
    text_policy_tests();
    review2_tests();
    review4_tests();
    game_issue_tests();
    history_tests();
    selftest_tests();
    ui_tests();
    std::cout << g_pass << " Pruefungen ok, " << g_fail << " fehlgeschlagen\n";
    return g_fail ? 1 : 0;
}
