// SPDX-License-Identifier: GPL-3.0-or-later
// Tests fuer Bericht, Anonymisierung und QR-Code.
#include "check.hpp"
#include "medic/collectors.hpp"
#include "medic/qr.hpp"
#include "medic/report.hpp"

#include <algorithm>
#include <string>

using namespace medic;
using nlohmann::json;

namespace {

bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

void anonymize_tests() {
    auto same = [](const std::string& s) { return anonymize(s) == s; };

    // IPv4
    CHECK(anonymize("http://192.168.1.94:10101") == "http://[IP]:10101", "IPv4 in URL");
    CHECK(anonymize("api=0.0.0.0:10101") == "api=[IP]:10101", "IPv4 0.0.0.0");
    CHECK(anonymize("von 10.0.0.1 nach 10.0.0.2.") == "von [IP] nach [IP].", "mehrere IPv4, Satzpunkt danach");
    CHECK(same("kstuff 1.10.2, FW 12.00, SM+ v1.7beta11"), "Versionen bleiben");
    CHECK(same("v1.2.3.4"), "Version mit vier Teilen (Buchstabe davor) bleibt");
    CHECK(same("1.2.3.4.5"), "fuenf Teile bleiben");
    CHECK(same("999.1.1.1") && same("192.168.1.300"), "Oktett > 255 bleibt");

    // MAC
    CHECK(anonymize("mac 00:1A:2B:3C:4D:5E ok") == "mac [MAC] ok", "MAC mit Doppelpunkt");
    CHECK(anonymize("00-1a-2b-3c-4d-5e") == "[MAC]", "MAC mit Bindestrich");
    CHECK(same("00:1A:2B:3C:4D"), "fuenf Gruppen sind keine MAC");

    // IPv6
    CHECK(anonymize("addr fe80::1ff:fe23:4567:890a end") == "addr [IPV6] end", "IPv6 komprimiert");
    CHECK(anonymize("2001:db8::1") == "[IPV6]", "IPv6 kurz");
    CHECK(anonymize("2001:0db8:85a3:0000:0000:8a2e:0370:7334") == "[IPV6]", "IPv6 voll");
    CHECK(same("[19:04:13] fertig") && same("12:30:45:10"), "Uhrzeiten bleiben");
    CHECK(same("std::bad_alloc und a::b_c"), "C++-Namen bleiben");

    // Benutzer-/Konto-IDs, Seriennummern
    CHECK(anonymize("/user/home/1e8f2c13/savedata") == "/user/home/[ID]/savedata", "Benutzerordner");
    CHECK(same("/user/home/abc/x") && same("/user/home/1e8f2c133/x"), "Benutzerordner nur mit genau 8 Hex-Ziffern");
    CHECK(anonymize("account id: 0123456789abcdef") == "account id: [ID]", "Schluessel account id");
    CHECK(anonymize("user_id=513995993 ok") == "user_id=[ID] ok", "Schluessel user_id");
    CHECK(anonymize("Serial: ABC-123, Rest") == "Serial: [SERIAL], Rest", "Schluessel Serial");
    CHECK(anonymize("serial number = X1 Y") == "serial number = [SERIAL] Y", "laengerer Schluessel zuerst");
    CHECK(same("Seriennummern wurden entfernt") && same("serialize: yes"), "Schluesselwoerter ohne Wert/Wortgrenze bleiben");
    CHECK(anonymize("id 0123456789ABCDEF end") == "id [ACCOUNT-ID] end", "16 Hex-Ziffern");
    CHECK(anonymize("0x0123456789abcdef") == "[ACCOUNT-ID]", "16 Hex-Ziffern mit 0x");
    CHECK(same("1794060571000000"), "16-stellige Dezimalzahl (Zeitstempel) bleibt");
    CHECK(same("0123456789abcdef0") && same("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "17 Ziffern und Hashes bleiben");
    CHECK(same("Größe: 5 GB, äöü, Pfad /mnt/usb0/homebrew/Spiel (5).ffpfsc"), "UTF-8 und Pfade bleiben");
}

Finding mk(const std::string& id, Severity s, const std::vector<std::string>& ev, const std::string& fix = "") {
    Finding f;
    f.rule_id = id;
    f.severity = s;
    f.text_de = "Deutscher Text " + id;
    f.text_en = "English text " + id;
    f.evidence = ev;
    f.fix = fix;
    f.source = "https://example.org/" + id;
    f.verified = "community";
    return f;
}

ReportInput sample_input() {
    ReportInput in;
    in.created_utc = "20261108T190500Z";
    in.rules_version = "2026.11.08";
    in.rule_count = 22;
    in.findings = {mk("smp.a", Severity::Critical, {"/mnt/usb0/homebrew/Rennspiel (5).ffpfsc", "http://192.168.1.94:10101/x", "/user/home/1e8f2c13/a"}, "rename_image_parentheses"),
                   mk("hen.b", Severity::Info, {"45 `x`"})};
    in.findings[0].evidence_path = "smp.images[].path";
    in.findings[1].evidence_path = "jb.seconds_after_boot";
    in.facts.set("fw.version", "12.00");
    in.facts.set("fw.model", "CFI-7021 B01Y");
    in.facts.set("kstuff.version", "1.10");
    in.facts.set("kstuff.origin", "drakmor");
    in.facts.set("smp.version", "1.7beta11");
    in.facts.set("proc.running", json::array({"elfldr", "kstuff", "shadowmountplus", "scesysavcontrol", "kstuff"}));
    in.facts.set("storage.mounts", json::array({{{"mount", "/data"}, {"total", 2000000000000ull}, {"free", 1350000000000ull}},
                                                {{"mount", "/mnt/usb9"}, {"total", 0}, {"free", 0}},
                                                {{"mount", "/mnt/usb1"}, {"total", 65536}, {"free", 52000}}}));
    TxnInfo t;
    t.txn = "20261108T180341Z";
    t.fix = "move_duplicate_titleid";
    t.rule = "smp.duplicate_titleid";
    t.state = "applied";
    in.history = {t};
    return in;
}

void report_tests_content() {
    const ReportInput in = sample_input();
    const std::string de = build_report(in, ReportLang::De), en = build_report(in, ReportLang::En);

    CHECK(has(de, "# PS5 Medic – Bericht") && has(de, "Erstellt: 2026-11-08 19:05 UTC"), "Bericht DE: Kopf und Zeit");
    CHECK(has(de, "Regelbasis 2026.11.08 (22)"), "Bericht DE: Regelbasis");
    CHECK(has(de, "Firmware: 12.00") && has(de, "Modell: CFI-7021 B01Y"), "Bericht DE: Firmware und Modell");
    CHECK(has(de, "kstuff: 1.10 (drakmor)") && has(de, "ShadowMount+: 1.7beta11"), "Bericht DE: Versionen");
    CHECK(has(de, "Laufende Payloads: elfldr, kstuff, shadowmountplus\n"), "Bericht DE: nur bekannte Payloads, ohne Doppelte");
    CHECK(!has(de, "scesysavcontrol"), "Bericht DE: Systemprozesse nicht aufgelistet");
    CHECK(has(de, "Speicher /data: 1350.0 GB frei von 2000.0 GB (68 %)") && !has(de, "usb9") && !has(de, "usb1"),
          "Bericht DE: Speicher, leere und winzige Mountpunkte weg");
    CHECK(has(de, "## Befunde (2)") && has(de, "### [KRITISCH] smp.a") && has(de, "### [HINWEIS] hen.b"), "Bericht DE: Befunde mit Schweregrad");
    CHECK(has(de, "Deutscher Text smp.a") && !has(de, "English text"), "Bericht DE: Text in der gewaehlten Sprache");
    CHECK(has(de, "Evidenz (path): `/mnt/usb0/homebrew/Rennspiel (5).ffpfsc`"), "Bericht DE: Evidenz mit Faktname im Code-Format, Dateiname bleibt");
    CHECK(has(de, "Reparatur: `rename_image_parentheses`") && has(de, "Quelle: https://example.org/smp.a (Community-Meldung)"), "Bericht DE: Reparatur und Quelle");
    CHECK(has(de, "Evidenz (seconds_after_boot): `45 'x'`"), "Bericht DE: Backticks in der Evidenz entschaerft");
    CHECK(has(de, "20261108T180341Z: move_duplicate_titleid (smp.duplicate_titleid) – angewendet"), "Bericht DE: Reparaturverlauf");
    CHECK(has(de, "Anonymisiert:"), "Bericht DE: Hinweis zur Anonymisierung");
    CHECK(!has(de, "192.168") && has(de, "http://[IP]:10101/x") && has(de, "/user/home/[ID]/a"), "Bericht DE: IP und Benutzer-ID anonymisiert");

    CHECK(has(en, "# PS5 Medic – Report") && has(en, "### [CRITICAL] smp.a") && has(en, "English text smp.a") && has(en, "Storage /data:"), "Bericht EN");
    CHECK(has(en, "(community report)") && has(en, "Anonymized:"), "Bericht EN: Quelle und Hinweis");

    ReportInput none = in;
    none.findings.clear();
    none.history.clear();
    const std::string empty = build_report(none, ReportLang::De);
    CHECK(has(empty, "## Befunde (0)") && has(empty, "Keine Befunde.") && !has(empty, "Reparaturen dieser Konsole"), "Bericht ohne Befunde und ohne Verlauf");

    ReportInput unknown;
    const std::string bare = build_report(unknown, ReportLang::De);
    CHECK(has(bare, "Firmware: unbekannt"), "Bericht ohne Fakten: Firmware unbekannt");
}

void compact_tests() {
    ReportInput in = sample_input();
    in.saved_path = "/data/ps5medic/reports/report_20261108T190500Z.md";
    bool truncated = true;
    const std::string c = build_report_compact(in, ReportLang::De, 640, &truncated);
    CHECK(c.size() <= 640 && !truncated, "Kurzfassung: passt, nicht gekuerzt");
    CHECK(has(c, "PS5 Medic ") && has(c, "FW 12.00 CFI-7021 B01Y") && has(c, "kstuff 1.10, SM+ 1.7beta11"), "Kurzfassung: Kopf");
    CHECK(has(c, "2 Befund(e):") && has(c, "[KRITISCH] smp.a") && has(c, "[HINWEIS] hen.b"), "Kurzfassung: Befunde");
    CHECK(has(c, " > path: /mnt/usb0/homebrew/Rennspiel (5).ffpfsc") && has(c, " > seconds_after_boot: 45 `x`") && !has(c, "192.168"),
          "Kurzfassung: Evidenz mit Faktname, anonymisiert");
    const std::string footer = "Bericht: /data/ps5medic/reports/report_20261108T190500Z.md\n";
    CHECK(c.size() >= footer.size() && c.compare(c.size() - footer.size(), footer.size(), footer) == 0, "Kurzfassung: Pfad des vollen Berichts am Ende");
    CHECK(std::all_of(c.begin(), c.end(), [](unsigned char ch) { return ch < 0x80; }), "Kurzfassung: reines ASCII (kleiner QR-Code)");

    // viele Befunde: wird an Befund-Grenzen gekuerzt, Kopf und Pfad bleiben
    ReportInput many = in;
    for (int i = 0; i < 40; ++i) many.findings.push_back(mk("rule.n" + std::to_string(i), Severity::Warning, {"/data/x/" + std::to_string(i)}));
    const std::string m = build_report_compact(many, ReportLang::En, 640, &truncated);
    CHECK(truncated && m.size() <= 640, "Kurzfassung: viele Befunde werden gekuerzt");
    CHECK(has(m, "... more findings in the report") && has(m, "Report: /data/ps5medic/reports/report_20261108T190500Z.md\n"), "Kurzfassung: Hinweis und Pfad bleiben");
    CHECK(has(m, "42 finding(s):") && has(m, "[CRITICAL] smp.a"), "Kurzfassung: die Zahl nennt alle Befunde, die Reihenfolge der Eingabe bleibt");

    // sehr kleine Grenze: nie laenger als erlaubt
    CHECK(build_report_compact(many, ReportLang::De, 120, &truncated).size() <= 120 && truncated, "Kurzfassung: harte Obergrenze");

    CHECK(report_path_for("20261108T190500Z") == "/data/ps5medic/reports/report_20261108T190500Z.md", "Berichtspfad");
    CHECK(report_path_for("../x").empty() && report_path_for("").empty() && report_path_for("a/b").empty(), "Berichtspfad: ungueltige Zeitstempel abgelehnt");
}

void unknown_lines_tests() {
    Facts f;
    f.set("smp.log.lines", json::array({
        "[15:12:42] ShadowMount+ v1.7beta11 started",
        "[18:49:25]   [LINK] image source cache update failed: /mnt/usb0/homebrew/Rennspiel 5.ffpfsc -> /mnt/shadowmnt/PPSA11112_a0a12b93",
        "[20:38:44]   [LINK] image source cache update failed: /mnt/usb0/homebrew/Rennspiel 5.ffpfsc -> /mnt/shadowmnt/PPSA11112_547c406a",
        "[20:38:45]   [IMG][LVD] unmount complete: source=/mnt/usb0/homebrew/X.exfat unit=3 error=0",
        "[20:38:46]   [MDBG] monitoring game errors: PPSA11112 pid=212 for up to 30 seconds",
        "[20:38:47]   [MDBG] game error monitoring expired: PPSA11112 pid=212",
        "[20:38:48]   [IMG] mount: error=22 for /mnt/usb0/a.exfat",
        "[20:38:49]   [IMG] mount: error=0x16 for /mnt/usb0/a.exfat",
        "[20:38:50]   [LINK] nullfs mounted: /mnt/shadowmnt/Jump-and-Run 4_be3f4f -> /system_ex/app/PPSA01234",
        "[20:38:51]   ERROR: could not open config",
        "[20:38:52]   Register failed for PPSA11112",
        "[20:38:53]   write FAILED (Timed Out) on /data/x",
    }));
    Finding explained;
    explained.rule_id = "smp.register_failed";
    explained.evidence_path = "smp.log.lines";
    explained.evidence = {"[20:38:52]   Register failed for PPSA11112"};
    Finding other_fact;  // Evidenz aus einem anderen Fakt erklaert keine Logzeile
    other_fact.rule_id = "x";
    other_fact.evidence_path = "proc.running";
    other_fact.evidence = {"[20:38:51]   ERROR: could not open config"};

    const auto u = unknown_error_lines(f, {explained, other_fact});
    CHECK(u.size() == 4, "Unbekannte Zeilen: vier Gruppen (Cache-Fehler, Mount-Fehler, ERROR, Timeout)");
    if (u.size() == 4) {
        CHECK(u[0].count == 2 && has(u[0].example, "image source cache update failed") && has(u[0].example, "[18:49:25]"),
              "Unbekannte Zeilen: gleichartige Zeilen (nur Kennung verschieden) bilden eine Gruppe, haeufigste zuerst, erste Zeile als Beispiel");
        CHECK(u[1].count == 2 && has(u[1].example, "error=22"), "Unbekannte Zeilen: error=22 und error=0x16 sind Fehlerwerte, dieselbe Gruppe");
        CHECK(has(u[2].example, "ERROR: could not open config") && u[2].count == 1, "Unbekannte Zeilen: grossgeschriebenes ERROR zaehlt, Evidenz aus anderem Fakt erklaert nichts");
        CHECK(has(u[3].example, "Timed Out"), "Unbekannte Zeilen: Fehlerwoerter ohne Beachtung der Gross-/Kleinschreibung");
    }
    std::string all;
    for (const auto& x : u) all += x.example + "\n";
    CHECK(!has(all, "error=0\n") && !has(all, "monitoring game errors") && !has(all, "game error monitoring") && !has(all, "Jump-and-Run") &&
              !has(all, "Register failed") && !has(all, "ShadowMount+ v1.7beta11"),
          "Unbekannte Zeilen: error=0, 'game error', Spieltitel, schon erklaerte Zeilen und Startzeile zaehlen nicht");

    Facts many;
    json arr = json::array();
    for (int i = 0; i < 30; ++i) arr.push_back("[10:00:00] step " + std::string(static_cast<std::size_t>(i + 1), 'x') + " failed");  // 30 verschiedene Zeilen
    many.set("smp.log.lines", arr);
    CHECK(unknown_error_lines(many, {}).size() == 10 && unknown_error_lines(many, {}, 3).size() == 3, "Unbekannte Zeilen: Obergrenze der Gruppen");
    CHECK(unknown_error_lines(Facts{}, {}).empty(), "Unbekannte Zeilen: ohne Log keine");

    ReportInput in = sample_input();
    in.unknown_lines = u;
    in.saved_path = "/data/ps5medic/reports/report_20261108T190500Z.md";
    const std::string de = build_report(in, ReportLang::De), en = build_report(in, ReportLang::En);
    CHECK(has(de, "## Unbekannte Fehlerzeilen (4)") && has(de, "- 2× `[18:49:25]   [LINK] image source cache update failed:"), "Bericht DE: Abschnitt mit Anzahl und Beispielzeile");
    CHECK(has(en, "## Unknown error lines (4)") && has(en, "belong in an issue"), "Bericht EN: Abschnitt");
    CHECK(has(de, "/mnt/shadowmnt/PPSA11112_a0a12b93") && !has(de, "192.168"), "Bericht: Beispielzeile bleibt unveraendert, Anonymisierung gilt trotzdem");
    CHECK(has(build_report_compact(in, ReportLang::De, 640), "4 unbekannte Fehlerzeile(n) im Bericht") && has(build_report_compact(in, ReportLang::En, 640), "4 unknown error line(s) in the report"),
          "Kurzfassung: Hinweis auf die unbekannten Zeilen");
    ReportInput none = sample_input();
    CHECK(!has(build_report(none, ReportLang::De), "Unbekannte Fehlerzeilen") && !has(build_report_compact(none, ReportLang::De, 640), "unbekannte"), "Ohne unbekannte Zeilen kein Abschnitt");
}

void from_fixture_test() {
    const std::string fx = std::string(MEDIC_SOURCE_DIR) + "/tests/fixtures/b2_pos";
    auto p = make_snapshot_platform(fx);
    Facts facts = collect_all(*p);
    auto rules = load_rules_from_file(std::string(MEDIC_SOURCE_DIR) + "/knowledge/rules.json");
    auto findings = evaluate(rules, facts);
    const ReportInput in = make_report_input(*p, facts, rules, findings, "20261108T190500Z");
    CHECK(in.rule_count == rules.rules.size() && in.rules_version == rules.db_version, "make_report_input: Regeln");
    const std::string r = build_report(in, ReportLang::En);
    CHECK(has(r, "smp.parentheses_in_pfs_name") && has(r, "Firmware: 4.03"), "Bericht aus Fixture b2_pos");
}

void qr_tests() {
    QrCode qr;
    CHECK(qr_encode("HELLO", qr) && qr.size == 21, "QR: kurzer Text -> Version 1 (21 Module)");
    if (qr.size == 21) {
        // Suchmuster in drei Ecken: 7x7 Ring dunkel, 5x5 Ring hell, 3x3 Kern dunkel
        auto finder = [&](int ox, int oy) {
            for (int d = 0; d < 7; ++d)
                if (!qr.dark(ox + d, oy) || !qr.dark(ox + d, oy + 6) || !qr.dark(ox, oy + d) || !qr.dark(ox + 6, oy + d)) return false;
            for (int d = 1; d < 6; ++d)
                if (qr.dark(ox + d, oy + 1) || qr.dark(ox + d, oy + 5) || qr.dark(ox + 1, oy + d) || qr.dark(ox + 5, oy + d)) return false;
            return qr.dark(ox + 3, oy + 3) && qr.dark(ox + 2, oy + 2) && qr.dark(ox + 4, oy + 4);
        };
        CHECK(finder(0, 0) && finder(14, 0) && finder(0, 14), "QR: Suchmuster in den drei Ecken");
        CHECK(qr.dark(8, 13), "QR: festes dunkles Modul");
        bool timing = true;
        for (int i = 8; i < 13; ++i) timing = timing && qr.dark(i, 6) == (i % 2 == 0) && qr.dark(6, i) == (i % 2 == 0);
        CHECK(timing, "QR: Taktmuster");
        CHECK(!qr.dark(-1, 0) && !qr.dark(21, 0) && !qr.dark(0, 99), "QR: ausserhalb ist hell");
    }

    // Die Kurzfassung eines grossen Berichts passt in einen QR-Code mit handhabbarer Groesse (<= Version 20 = 97 Module).
    ReportInput many = sample_input();
    for (int i = 0; i < 40; ++i) many.findings.push_back(mk("rule.n" + std::to_string(i), Severity::Warning, {"/data/x/" + std::to_string(i)}));
    many.saved_path = "/data/ps5medic/reports/report_20261108T190500Z.md";
    QrCode big;
    CHECK(qr_encode(build_report_compact(many, ReportLang::De, 640), big) && big.size >= 41 && big.size <= 97 && (big.size - 17) % 4 == 0,
          "QR: Kurzfassung (<= 640 Byte) ergibt hoechstens Version 20");

    QrCode utf;
    CHECK(qr_encode("Größe äöü ✓", utf) && utf.size >= 21, "QR: UTF-8");
    QrCode too_long = big;
    CHECK(!qr_encode(std::string(5000, 'x'), too_long) && too_long.size == 0 && too_long.modules.empty(), "QR: zu langer Text wird abgelehnt");

    const std::string text = qr_to_text(qr);
    CHECK(!text.empty() && std::count(text.begin(), text.end(), '\n') == (qr.size + 4 + 1) / 2, "QR: Textdarstellung hat die erwartete Zeilenzahl");
}

}  // namespace

void report_tests() {
    anonymize_tests();
    report_tests_content();
    compact_tests();
    unknown_lines_tests();
    from_fixture_test();
    qr_tests();
}
