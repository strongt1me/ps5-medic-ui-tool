// SPDX-License-Identifier: GPL-3.0-or-later
// Tests zu den Funden der vierten Pruefrunde (13. und 14. Nov. 2026): Lesen grosser Logs, JSON-Ausgabe mit ungueltigem UTF-8, Datenbanken unter 100 Byte,
// Fehlbefund durch .dat-Dateien, Spiele-Zuordnung, applies_fw, Groessenpruefung beim Beiseitelegen, Symlinks, Selbsttest-Dateien, Schwaerzung, Textregel,
// Meldungen der Fix-Engine, Parameter der UI-Sprachen.
#include "check.hpp"
#include "medic/checks.hpp"
#include "medic/collectors.hpp"
#include "medic/core_messages.hpp"
#include "medic/fixes.hpp"
#include "medic/games.hpp"
#include "medic/jsondump.hpp"
#include "medic/posix_fs.hpp"
#include "medic/readtail.hpp"
#include "medic/report.hpp"
#include "medic/rules.hpp"
#include "medic/selftest.hpp"
#include "medic/snapshot_export.hpp"
#include "medic/text_policy.hpp"
#include "testutil.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <string>

using namespace medic;
using nlohmann::json;
using testutil::Sandbox;
using testutil::slurp;
using testutil::spit;

namespace {

namespace stdfs = std::filesystem;

void read_tail_tests() {
    Sandbox sb;
    std::string big;
    for (int i = 0; i < 400000; ++i) big += "line " + std::to_string(i) + "\n";  // rund 4,6 MB
    spit(sb.real("/data/shadowmount/debug.log"), big);
    spit(sb.real("/data/shadowmount/empty.log"), "");
    PosixFs fs(sb.dir / "fs");
    std::string out;
    CHECK(read_tail(fs, "/data/shadowmount/debug.log", 1u << 20, out), "read_tail: grosse Datei lesbar");
    CHECK(out.size() <= (1u << 20) && out.size() > (900u << 10), "read_tail: hoechstens 1 MiB vom Ende");
    CHECK(out.compare(0, 5, "line ") == 0 && out.size() > 12 && out.compare(out.size() - 12, 12, "line 399999\n") == 0, "read_tail: beginnt an einer Zeilengrenze, endet mit der letzten Zeile");
    CHECK(read_tail(fs, "/data/shadowmount/empty.log", 1u << 20, out) && out.empty(), "read_tail: leere Datei ist lesbar und leer");
    CHECK(!read_tail(fs, "/data/shadowmount/fehlt.log", 1u << 20, out), "read_tail: fehlende Datei");
    std::string line(5000, 'x');
    cap_line(line);
    CHECK(line.size() == 2000, "cap_line: Zeilen auf 2000 Zeichen begrenzt");
    std::string short_line = "kurz";
    cap_line(short_line);
    CHECK(short_line == "kurz", "cap_line: kurze Zeile bleibt unveraendert");
    std::string own_cap(50, 'y');
    cap_line(own_cap, 10);
    CHECK(own_cap.size() == 10, "cap_line: eigene Obergrenze");

    // Datei kleiner als max_bytes (haeufigster Weg): vollstaendig, ohne Zeilenverlust
    spit(sb.real("/data/shadowmount/small.log"), "a\nb\nc\n");
    CHECK(read_tail(fs, "/data/shadowmount/small.log", 1u << 20, out) && out == "a\nb\nc\n", "read_tail: kleine Datei vollstaendig");
    // Datei groesser als max_bytes: der angeschnittene Anfang bis zum ersten Zeilenumbruch entfaellt
    CHECK(read_tail(fs, "/data/shadowmount/small.log", 3, out) && out == "c\n", "read_tail: gekuerzt, Teilzeile am Anfang verworfen");
    CHECK(read_tail(fs, "/data/shadowmount/small.log", 4, out) && out == "b\nc\n", "read_tail: beginnt der Ausschnitt an einem Zeilenanfang, bleibt die erste Zeile");
    spit(sb.real("/data/shadowmount/two.log"), "xxxxxxxxxx\nyyyyyyyyy\n");  // Ende genau eine Zeile (10 Byte): nicht leer werden
    CHECK(read_tail(fs, "/data/shadowmount/two.log", 10, out) && out == "yyyyyyyyy\n", "read_tail: letzte Zeile, die genau max_bytes fuellt, bleibt");

    // Eine einzige riesige Zeile im SM+-Log darf die Pruefung nicht aus dem Tritt bringen (std::regex ist rekursiv)
    Sandbox sb2;
    spit(sb2.real("/data/shadowmount/debug.log"), "[CFG] invalid value at line 3: depth=9\n[CFG] invalid " + std::string(300000, 'a') + "\n");
    auto p = make_snapshot_platform(sb2.dir.string());
    Facts f;
    collect_shadowmount(*p, f);
    const json* lines = f.get("smp.log.lines");
    bool capped = lines && lines->is_array() && lines->size() == 2;
    if (capped)
        for (const auto& l : *lines) capped = capped && l.get<std::string>().size() <= 2000;
    CHECK(capped, "Log: Zeilen werden auf 2000 Zeichen gekuerzt");
}

void dump_safe_tests() {
    json j = {{"pfad", std::string("/data/homebrew/Cool (\xE4).ffpfs")}};
    bool threw = false;
    std::string s;
    try {
        s = dump_safe(j, 1);
    } catch (...) {
        threw = true;
    }
    CHECK(!threw && s.find("Cool") != std::string::npos, "dump_safe: ungueltiges UTF-8 wirft nicht");

    // save_check mit so einem Pfad als Evidenz
    Sandbox sb;
    auto p = make_snapshot_platform(sb.dir.string(), true);
    Facts facts;
    Finding fi;
    fi.rule_id = "t.rule";
    fi.severity = Severity::Warning;
    fi.evidence = {std::string("/data/homebrew/Cool (\xE4).ffpfs")};
    StoredCheck rec;
    bool ok = false;
    try {
        rec.ts = "20261114T000000Z";
        rec.findings = {fi};
        ok = save_check(*p, rec);
    } catch (...) {
        ok = false;
    }
    CHECK(ok, "save_check: Evidenz mit ungueltigem UTF-8 wird gespeichert");
}

void collector_tests() {
    // Datenbank unter 100 Byte: kein SQLite-Kopf, also ein Befund statt Schweigen
    {
        Sandbox sb;
        spit(sb.real("/system_data/priv/mms/app.db"), std::string(20, 'x'));
        spit(sb.real("/system_data/priv/mms/empty.db"), "");
        auto p = make_snapshot_platform(sb.dir.string());
        Facts f;
        collect_databases(*p, f);
        const json* files = f.get("db.files");
        bool short_bad = false;
        if (files && files->is_array())
            for (const auto& d : *files)
                if (d.value("name", std::string()) == "app.db") short_bad = d["readable"] == 1 && d["header_bad"] == 1;
        CHECK(short_bad, "Datenbank: 20-Byte-Datei gilt als lesbar mit ungueltigem Kopf");
    }

    // .dat-Dateien aus "System Volume Information" und tiefer als die Scantiefe sind keine Spiele
    {
        Sandbox sb;
        spit(sb.real("/data/shadowmount/config.ini"), "[config]\nversion = 2\n");
        spit(sb.real("/mnt/usb0/System Volume Information/WPSettings.dat"), std::string(12, 'x'));
        spit(sb.real("/mnt/usb0/Sammlung/WPSettings.dat"), std::string(12, 'x'));
        spit(sb.real("/mnt/usb0/Sammlung/Game.exfat"), std::string(2000, 'x'));
        auto p = make_snapshot_platform(sb.dir.string());
        Facts f;
        collect_shadowmount(*p, f);
        const json* deep = f.get("smp.deep_sources");
        bool dat = false, exfat = false;
        if (deep && deep->is_array())
            for (const auto& d : *deep) {
                const std::string path = d.value("path", std::string());
                dat = dat || path.find(".dat") != std::string::npos;
                exfat = exfat || path.find("Game.exfat") != std::string::npos;
            }
        CHECK(!dat, ".dat ohne Titel-ID erzeugt keinen scan_depth-Befund");
        CHECK(exfat, "ein tiefes Image mit echter Endung bleibt ein scan_depth-Befund");
    }
}

void games_tests() {
    Facts facts;
    facts.set("smp.titles", json::array({json{{"titleid", "PPSA00001"}, {"path", "/data/homebrew/God of War"}, {"root", "/data/homebrew"}, {"level", 1}},
                                         json{{"titleid", "PPSA00002"}, {"path", "/data/homebrew/God of War Ragnarok"}, {"root", "/data/homebrew"}, {"level", 1}}}));
    Finding f;
    f.rule_id = "t.rule";
    f.severity = Severity::Warning;
    f.evidence = {"/data/homebrew/God of War Ragnarok/eboot.bin"};
    int first = 0, second = 0;
    for (const auto& g : collect_game_infos(facts, {f})) {
        if (g.titleid == "PPSA00001" && !g.findings.empty()) ++first;
        if (g.titleid == "PPSA00002" && !g.findings.empty()) ++second;
    }
    CHECK(first == 0 && second == 1, "Spiele: Befund fuer 'God of War Ragnarok' gehoert nicht zu 'God of War'");
    Finding f2;
    f2.rule_id = "t.rule2";
    f2.severity = Severity::Warning;
    f2.evidence = {"mount failed: /data/homebrew/God of War code=0x1"};
    int hit = 0;
    for (const auto& g : collect_game_infos(facts, {f2}))
        if (g.titleid == "PPSA00001" && !g.findings.empty()) ++hit;
    CHECK(hit == 1, "Spiele: Pfad mit Leerzeichen vor einer Logzeile bleibt zugeordnet");
}

bool rejects_rule(const std::string& fw) {
    const std::string text = R"({"schema_version":1,"db_version":"t","rules":[{"id":"t.fw","severity":"info","applies_fw":")" + fw +
                             R"(","when":{"fact":"x","eq":"1"},"text":{"de":"d","en":"e"},"source":"https://example.org","verified":"doc"}]})";
    try {
        load_rules_from_string(text);
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
}

void rules_tests() {
    for (const char* bad : {">=13,60", "13.60-14.10", "=>13.60", "!=13.60", ">=", "garbage", "", ">=13.60 <", ">=13."})
        CHECK(rejects_rule(bad), std::string("applies_fw ungueltig: '") + bad + "'");
    for (const char* good : {">=13.60", ">= 13.60", ">=4.00 <=7.61", "=9.00", "12.00", "<14.10", ">=1.7beta12"})
        CHECK(!rejects_rule(good), std::string("applies_fw gueltig: '") + good + "'");
}

void quarantine_size_tests() {
    // Ein beiseitegelegtes Image oder eine ELF-Datei darf sich bis zur Bestaetigung nicht veraendert haben (die Uebertragung lief evtl. noch)
    Sandbox sb;
    spit(sb.real("/data/homebrew/x.elf"), "abc");
    auto p = make_snapshot_platform(sb.dir.string(), true);
    Facts facts;
    facts.set("smp.scan_roots", json::array({"/data/homebrew"}));
    facts.set("payload.broken_elfs", json::array({json{{"path", "/data/homebrew/x.elf"}, {"reason", "too_small"}}}));
    Finding fi;
    fi.rule_id = "payload.elf_invalid";
    fi.fix = "quarantine_broken_elf";
    const FixPlan plan = plan_fix(fi, facts, p->fs(), {});
    CHECK(plan.changes.size() == 1 && plan.changes[0].check_size && plan.changes[0].base_size == 3, "Quarantaene: Groesse der Datei beim Planen gemerkt");
    spit(sb.real("/data/homebrew/x.elf"), std::string(100000, 'e'));  // die Uebertragung ist inzwischen fertig
    ApplyOptions o;
    o.timestamp = "20261114T000000Z";
    o.confirmed = true;
    const ApplyResult r = apply_plan(plan, *p, policy_from_facts(facts), o);
    CHECK(!r.ok && r.error.find("geändert") != std::string::npos, "Quarantaene: veraenderte Datei wird abgelehnt (" + r.error + ")");
    CHECK(stdfs::exists(sb.real("/data/homebrew/x.elf")) && !stdfs::exists(sb.real("/data/homebrew/.medic_broken/x.elf")), "Quarantaene: die Datei bleibt, wo sie ist");

    spit(sb.real("/data/homebrew/y.elf"), "abc");
    facts.set("payload.broken_elfs", json::array({json{{"path", "/data/homebrew/y.elf"}, {"reason", "too_small"}}}));
    const FixPlan plan2 = plan_fix(fi, facts, p->fs(), {});
    o.timestamp = "20261114T000100Z";
    const ApplyResult r2 = apply_plan(plan2, *p, policy_from_facts(facts), o);
    CHECK(r2.ok && stdfs::exists(sb.real("/data/homebrew/.medic_broken/y.elf")), "Quarantaene: unveraenderte Datei wird verschoben (" + r2.error + ")");
}

void symlink_tests() {
    Sandbox sb;
    spit(sb.real("/data/homebrew/a.txt"), "a");
    stdfs::create_directories(sb.real("/user/app/victim_empty"));
    std::error_code ec;
    stdfs::create_directory_symlink(sb.real("/user/app"), sb.real("/data/homebrew/x"), ec);
    if (ec) return;  // Dateisystem ohne Symlinks
    PosixRw rw(sb.dir / "fs");
    CHECK(!rw.remove_empty_dir("/data/homebrew/x/victim_empty") && stdfs::exists(sb.real("/user/app/victim_empty")), "Symlink: leerer Ordner hinter einem Symlink wird nicht entfernt");
    stdfs::create_symlink("/nonexistent/target", sb.real("/data/homebrew/dang"), ec);
    CHECK(!rw.rename("/data/homebrew/a.txt", "/data/homebrew/dang") && stdfs::exists(sb.real("/data/homebrew/a.txt")), "Symlink: haengender Link als Ziel wird nicht ueberschrieben");
    spit(sb.real("/data/homebrew/real.ini"), "real");
    stdfs::create_symlink(sb.real("/data/homebrew/real.ini"), sb.real("/data/homebrew/config.ini"), ec);
    CHECK(!rw.write_file("/data/homebrew/config.ini", "neu") && stdfs::is_symlink(sb.real("/data/homebrew/config.ini")) && slurp(sb.real("/data/homebrew/real.ini")) == "real",
          "Symlink: Schreiben auf einen Link wird abgelehnt");
}

void selftest_file_tests() {
    Sandbox sb;
    spit(sb.real(kSelftestLastPath), R"({"finished":"20261114T000000Z","results":[{"id":"hw.x","status":5,"reason":7,"duration_s":"x","started":3,)"
                                     R"("measurements":[{"name":"a","value":1,"unit":3}]},{"id":"hw.y","status":"passed","duration_s":1.5}]})");
    spit(sb.real(kSelftestMarkerPath), R"({"state":4,"started":9})");
    auto p = make_snapshot_platform(sb.dir.string());
    bool threw = false;
    std::vector<TestResult> res;
    Facts f;
    try {
        res = load_last_selftest(p->fs());
        collect_selftest(*p, f);
    } catch (...) {
        threw = true;
    }
    CHECK(!threw && res.size() == 2, "Selbsttest-Datei mit falschen Typen wirft nicht");
}

void redact_tests() {
    CHECK(anonymize("inet6:fe80::1c2d:3e4f:5a6b:7c8d up").find("fe80") == std::string::npos, "Bericht: IPv6 hinter einer Beschriftung mit ':' geschwaerzt");
    CHECK(anonymize("addr:2001:db8::1 ok").find("2001") == std::string::npos, "Bericht: IPv6 hinter 'addr:' geschwaerzt");
    const std::string mapped = anonymize("peer ::ffff:192.168.1.5 closed");
    CHECK(mapped.find("168.1.5") == std::string::npos && mapped.find("[IPV6]") != std::string::npos, "Bericht: IPv4 in IPv6 vollstaendig geschwaerzt: " + mapped);
    CHECK(anonymize("std::bad_alloc und a::b_c").find("a::b_c") != std::string::npos, "Bericht: C++-Namen bleiben");
    CHECK(anonymize("savedata_0123456789abcdef/x").find("0123456789abcdef") == std::string::npos, "Bericht: Konto-ID hinter '_' geschwaerzt");
    CHECK(anonymize("dev_aa:bb:cc:dd:ee:ff_x").find("aa:bb:cc") == std::string::npos, "Bericht: MAC neben '_' geschwaerzt");
    const std::string pw = anonymize("password = my long phrase here\nnext line");
    CHECK(pw.find("phrase") == std::string::npos && pw.find("long") == std::string::npos && pw.find("next line") != std::string::npos, "Bericht: Passwort mit Leerzeichen ganz geschwaerzt: " + pw);
    CHECK(anonymize("Authorization: Basic dXNlcjpwYXNz").find("dXNlcjpwYXNz") == std::string::npos, "Bericht: Authorization geschwaerzt");
    CHECK(anonymize("psk=geheim123 ok").find("geheim123") == std::string::npos, "Bericht: psk geschwaerzt");
    CHECK(anonymize("onlineId: Spieler_1").find("Spieler_1") == std::string::npos, "Bericht: Online-ID geschwaerzt");
    CHECK(redact_ipv6("x fe80::1 y").find("fe80") == std::string::npos, "redact_ipv6");
    const std::string mapped2 = anonymize("peer ::ffff:192.168.1.5. closed");
    CHECK(mapped2.find("168.1.5") == std::string::npos && mapped2.find("closed") != std::string::npos, "Bericht: IPv4 in IPv6 mit Satzpunkt vollstaendig geschwaerzt: " + mapped2);
    const std::string psk = anonymize("psk = a b c\nnext");
    CHECK(psk.find("b c") == std::string::npos && psk.find("next") != std::string::npos, "Bericht: psk mit Leerzeichen ganz geschwaerzt: " + psk);

    // Export: eine IPv4 vor einem Satzpunkt und eine IPv6 bleiben nicht im Log
    Sandbox sb;
    spit(sb.real("/data/shadowmount/debug.log"), "Failed to connect to 192.168.1.5.\nfrom fe80::1c2d:3e4f:5a6b:7c8d ok\n");
    auto p = make_snapshot_platform(sb.dir.string());
    Facts facts = collect_all(*p);
    const std::string dest = (sb.dir / "export").string();
    const ExportResult r = export_snapshot(*p, facts, dest, "20261114T000000Z");
    CHECK(r.ok, "Export: ok (" + r.error + ")");
    const std::string log = slurp(stdfs::path(dest) / "fs" / "data" / "shadowmount" / "debug.log");
    CHECK(log.find("192.168.1.5") == std::string::npos && log.find("fe80") == std::string::npos && log.find("Failed to connect") != std::string::npos, "Export: IPv4 vor Satzpunkt und IPv6 geschwaerzt: " + log);
}

void text_policy_tests2() {
    for (const char* s : {"Installez le dernier micrologiciel.", "Installa l'ultimo firmware.", "Instala el último firmware.", "Instala el firmware más reciente.",
                          "Updating the firmware solves the problem.", "Firmware needs updating.", "Consider upgrading your console.",
                          "Update your PS5 to the latest version.", "Aktualisiere die Konsole auf die neueste Version.", "Update your console software.",
                          "Mettez votre console à jour.", "Aggiorna il sistema.", "Actualiza el sistema.", "Actualiza el software de la consola.", "Firmware aufspielen.",
                          "Die Systemsoftware neu installieren.", "Spiele eine neuere Firmware ein.",
                          "Update the\xC2\xA0system\xC2\xA0software.", "Update the system\xE2\x80\xAFsoftware.", "System\xE2\x80\x91Update einspielen", "System\xE2\x80\x93Update",
                          "Mettez a\xCC\x80 jour le micrologiciel.", "System Software\xC2\xA0Update and Settings"})
        CHECK(!find_system_update_advice(s).empty(), std::string("Textregel erkennt (Review 4): ") + s);
    // Jede Ausweichschreibweise, die normalized() beseitigt (Pruefung Runde 6): Leerzeichen-Arten ersetzen das Leerzeichen, unsichtbare Zeichen und
    // kombinierende Akzente stehen mitten im Wort, Bindestrich-Varianten statt "-"
    for (const char* sp : {"\xC2\xA0", "\xE2\x80\x80", "\xE2\x80\x83", "\xE2\x80\x8A", "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80"})
        CHECK(!find_system_update_advice(std::string("Update the") + sp + "system" + sp + "software.").empty(), std::string("Textregel: Leerzeichen-Variante U+") + std::to_string(static_cast<unsigned char>(sp[2])));
    for (const char* inv : {"\xC2\xAD", "\xE2\x80\x8B", "\xE2\x80\x8C", "\xE2\x80\x8D", "\xEF\xBB\xBF", "\xCD\x80", "\xCC\x80"})
        CHECK(!find_system_update_advice(std::string("Update the sys") + inv + "tem software.").empty(), std::string("Textregel: unsichtbares Zeichen mitten im Wort, Folgebyte ") + std::to_string(static_cast<unsigned char>(inv[1])));
    for (const char* dash : {"\xE2\x80\x90", "\xE2\x80\x91", "\xE2\x80\x92", "\xE2\x80\x93", "\xE2\x80\x94", "\xE2\x80\x95", "\xE2\x88\x92"})
        CHECK(!find_system_update_advice(std::string("System") + dash + "Update einspielen").empty(), std::string("Textregel: Bindestrich-Variante, Folgebyte ") + std::to_string(static_cast<unsigned char>(dash[2])));
    // Ohne Sonderzeichen (Schnellpfad ohne Kopie) bleibt das Ergebnis dasselbe wie mit einem harmlosen, normalisierten Zeichen am Ende
    for (const char* s : {"Update the system software.", "Installiere die neueste Firmware.", "Aktualisiere die PS5 Medic Regeln.", "Wenn die Firmware 13.60 ist, aktualisiere kstuff-lite."})
        CHECK(find_system_update_advice(s).empty() == find_system_update_advice(std::string(s) + "\xE2\x80\x8B").empty(), std::string("Textregel: Schnellpfad gleich Normalisierung: ") + s);
    // "Firmware ... Verb": ein frueheres Verb im selben Satz darf nicht von einem spaeteren Bestandteil ueberdeckt werden (Pruefung Runde 6, Fund 1)
    for (const char* s : {"Your firmware is too old: update it, then update kstuff.", "The firmware is outdated, you should update it or update kstuff.",
                          "Firmware mismatch - update it, then update payloads.", "Die Firmware ist zu alt, bitte hier jetzt aktualisieren, dann aktualisiere kstuff"})
        CHECK(!find_system_update_advice(s).empty(), std::string("Textregel erkennt Firmware-Verb vor Bestandteil: ") + s);
    // Eine Ausnahme fuer einen genannten Bestandteil darf echte Hinweise im selben Satz nicht verdecken
    for (const char* s : {"Update kstuff and the firmware.", "Update your games and the system software.", "Update payloads, plugins and the console firmware.",
                          "Update the apps and then the firmware", "Aktualisiere kstuff, ShadowMount+ und die Systemsoftware.", "Aktualisiere die Spiele und danach die Firmware.",
                          "Update kstuff and update the firmware."})
        CHECK(!find_system_update_advice(s).empty(), std::string("Textregel erkennt trotz Bestandteil: ") + s);
    for (const char* s : {"Update kstuff-lite to a version that supports firmware 13.60.", "Wenn die Firmware 13.60 ist, aktualisiere kstuff-lite.", "Update the console's DNS settings.",
                          "Update the PS5 Medic rules.", "Aktualisiere die PS5 Medic Regeln.", "Installiere den Treiber für das Laufwerk.", "Aggiorna il sistema di file se serve.",
                          "Actualiza el sistema de archivos.",
                          // "+" im Namen ist keine Konjunktion; ein Komma trennt den Teilsatz ueber die Konsole vom Befehl (Pruefung Runde 6, Fund 2 und 3)
                          "Update ShadowMount+ to a version that supports firmware 13.60.", "Aktualisiere ShadowMount+ fuer die Firmware 13.60",
                          "Wenn die Konsole auf einer neueren Firmware l\xC3\xA4uft, aktualisiere kstuff-lite.", "L\xC3\xA4uft die Konsole auf der Version 13, aktualisiere ShadowMount+."})
        CHECK(find_system_update_advice(s).empty(), std::string("Textregel erlaubt (Review 4): ") + s + " -> " + find_system_update_advice(s));
}

// Jede deutsche Meldung der Fix-Engine in fixes.cpp und engine.cpp hat eine englische Fassung (core_messages.cpp)
void core_message_tests() {
    int checked = 0;
    for (const char* name : {"fixes.cpp", "engine.cpp"}) {
        std::istringstream in(slurp(stdfs::path(MEDIC_SOURCE_DIR) / "libmedic" / "src" / name));
        std::string line;
        static const std::regex lit(R"re("((?:[^"\\]|\\.)*)")re");
        while (std::getline(in, line)) {
            const bool message = line.find("warnings.push_back(") != std::string::npos || line.find(" fail(") != std::string::npos || line.find("abort_all(") != std::string::npos ||
                                 line.find(" no(") != std::string::npos || line.find("err = ") != std::string::npos || line.find("why = ") != std::string::npos ||
                                 line.find("r.error = std::string(") != std::string::npos;
            if (!message || line.find("//") < line.find("\"")) continue;
            std::smatch m;
            if (!std::regex_search(line, m, lit)) continue;
            const std::string text = m[1].str();
            if (text.size() < 5 || text == "type" || text == "error") continue;  // Trenner wie ": " sind keine Meldung
            ++checked;
            CHECK(core_message_en(text) != text, std::string("Meldung hat eine englische Fassung: ") + text);
        }
    }
    CHECK(checked > 40, "Meldungstexte der Engine gefunden (" + std::to_string(checked) + ")");
    CHECK(core_message_en("Nichts zu tun: /data/x enthält den Wert bereits") == "Nothing to do: /data/x already has the value", "Meldung: Fragmente rund um einen Pfad");
    CHECK(core_message_en("Quelle nicht lesbar: /a | Rollback unvollständig: Zurückbenennen fehlgeschlagen: /b -- im Verlauf zurückholen") ==
              "Source not readable: /a | Rollback incomplete: Renaming back failed: /b -- restore from the history", "Meldung: zusammengesetzte Fehler");
    CHECK(core_message_en("Unbekannter Text").find("Unbekannter") != std::string::npos, "Meldung: Unbekanntes bleibt");
}

// Schluessel und Platzhalter der fuenf UI-Sprachen sind gleich
void i18n_parity_tests() {
    auto placeholders = [](const std::string& s) {
        std::multiset<std::string> out;
        static const std::regex ph(R"(\{[a-z_0-9]+\})");
        for (auto it = std::sregex_iterator(s.begin(), s.end(), ph); it != std::sregex_iterator(); ++it) out.insert(it->str());
        return out;
    };
    std::map<std::string, json> langs;
    for (const char* l : {"de", "en", "fr", "it", "es"}) langs[l] = json::parse(slurp(stdfs::path(MEDIC_SOURCE_DIR) / "ui" / "i18n" / (std::string(l) + ".json")));
    for (const char* l : {"en", "fr", "it", "es"}) {
        int missing = 0, extra = 0, ph_diff = 0;
        for (auto it = langs["de"].begin(); it != langs["de"].end(); ++it) {
            if (!langs[l].contains(it.key())) {
                ++missing;
                continue;
            }
            if (placeholders(it.value().get<std::string>()) != placeholders(langs[l][it.key()].get<std::string>())) {
                ++ph_diff;
                std::cerr << "Platzhalter " << l << ": " << it.key() << "\n";
            }
        }
        for (auto it = langs[l].begin(); it != langs[l].end(); ++it)
            if (!langs["de"].contains(it.key())) ++extra;
        CHECK(missing == 0 && extra == 0 && ph_diff == 0, std::string("UI-Sprache ") + l + ": Schluessel und Platzhalter wie Deutsch");
    }
}

// Prozessliste, die nicht gelesen werden konnte (sysctl ENOMEM): leer, aber readable() == false
struct UnreadableProcs final : IProcessList {
    bool ok;
    explicit UnreadableProcs(bool readable_) : ok(readable_) {}
    std::vector<std::string> running() const override { return {}; }
    bool readable() const override { return ok; }
};

struct ProcsPlatform final : Platform {
    std::unique_ptr<Platform> base;
    UnreadableProcs procs_;
    ProcsPlatform(std::unique_ptr<Platform> b, bool readable_) : base(std::move(b)), procs_(readable_) {}
    const IFileSystem& fs() const override { return base->fs(); }
    const IProcessList& procs() const override { return procs_; }
    const ISystemInfo& sys() const override { return base->sys(); }
};

// Ein Lesefehler der Prozessliste ist "unbekannt", nicht "es laeuft nichts" (Pruefung Runde 6, Fund 6)
void unreadable_procs_tests() {
    Sandbox sb;
    ProcsPlatform bad(make_snapshot_platform(sb.dir.string()), false);
    Facts f = collect_all(bad);
    CHECK(f.get("proc.running") == nullptr, "Prozessliste nicht lesbar: kein Fakt proc.running");
    bool failed_named = false;
    if (const json* fl = f.get("collector.failed"); fl && fl->is_array())
        for (const auto& e : *fl)
            if (e.value("name", "") == "collect_processes") failed_named = true;
    CHECK(failed_named, "Prozessliste nicht lesbar: Sammler als fehlgeschlagen vermerkt");
    ProcsPlatform empty(make_snapshot_platform(sb.dir.string()), true);
    Facts g = collect_all(empty);
    const json* run = g.get("proc.running");
    CHECK(run && run->is_array() && run->empty(), "Prozessliste lesbar und leer: Fakt proc.running ist eine leere Liste");
}

}  // namespace

void review4_tests() {
    unreadable_procs_tests();
    read_tail_tests();
    dump_safe_tests();
    collector_tests();
    games_tests();
    rules_tests();
    quarantine_size_tests();
    symlink_tests();
    selftest_file_tests();
    redact_tests();
    text_policy_tests2();
    core_message_tests();
    i18n_parity_tests();
}
