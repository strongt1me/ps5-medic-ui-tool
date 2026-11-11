// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des Selbsttest-Frameworks (libmedic/src/selftest.cpp): Registry, Statusmodell, Marker, Fakten.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/collectors.hpp"
#include "medic/platform.hpp"
#include "medic/report.hpp"
#include "medic/selftest.hpp"
#include "medic/text_policy.hpp"

#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
using namespace medic;
using nlohmann::json;

namespace {

using namespace testutil;

TestResult run(const char* id, const Facts& f, const Platform& p, const TestContext& ctx = {}) {
    const TestDef* d = find_test(id);
    if (!d) {
        TestResult r;
        r.status = TestStatus::Error;
        return r;
    }
    return run_selftest(p, f, *d, ctx);
}

double measure(const TestResult& r, const char* name) {
    for (const auto& m : r.measurements)
        if (m.name == name) return m.value;
    return -1;
}

struct Copy {
    fs::path dir;
    explicit Copy(const std::string& fixture) {
        static int n = 0;
        dir = fs::temp_directory_path() / ("medic-selftest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::copy(fs::path(MEDIC_SOURCE_DIR) / "tests/fixtures" / fixture, dir, fs::copy_options::recursive);
    }
    ~Copy() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

}  // namespace

void selftest_tests() {
    {  // Registry
        const auto& reg = selftest_registry();
        std::set<std::string> ids;
        bool order_ok = true, text_ok = true, risk_ok = true, saw_standard = false;
        for (const auto& d : reg) {
            ids.insert(d.id);
            if (d.tier == TestTier::Standard) saw_standard = true;
            else if (saw_standard) order_ok = false;  // Schnell vor Standard
            text_ok = text_ok && !d.name_de.empty() && !d.name_en.empty() && !d.proves_not_de.empty() && !d.proves_not_en.empty() &&
                      find_system_update_advice(d.name_de + d.proves_not_de + d.name_en + d.proves_not_en).empty();
            risk_ok = risk_ok && (d.risk == "none" || d.risk == "read");  // nur lesend
        }
        CHECK(reg.size() >= 5 && ids.size() == reg.size() && order_ok && text_ok && risk_ok, "Selbsttest: Registry (eindeutige IDs, Schnell vor Standard, nur lesend, Texte)");
        CHECK(find_test("sw.db.headers") && !find_test("gibt.es.nicht"), "Selbsttest: Test per ID finden");
        CHECK(test_status_from_string("failed") == TestStatus::Failed && test_status_from_string("quatsch") == TestStatus::NotRun, "Selbsttest: Statusnamen");
    }
    Sandbox sb;
    auto empty = make_snapshot_platform(sb.dir.string(), false);
    {  // Schnelltests aus Fakten: Statusmodell
        Facts f;
        CHECK(run("sw.storage.mounts", f, *empty).status == TestStatus::Skipped, "Selbsttest: Speicher ohne Daten = skipped, nie passed");
        f.set("storage.mounts", json::array({{{"mount", "/data"}, {"total", 9e11}, {"free_pct", 44.0}}}));
        auto r = run("sw.storage.mounts", f, *empty);
        CHECK(r.status == TestStatus::Passed && measure(r, "free_pct") == 44.0, "Selbsttest: Speicher ok");
        f.set("storage.mounts", json::array({{{"mount", "/data"}, {"total", 9e11}, {"free_pct", 3.0}}}));
        CHECK(run("sw.storage.mounts", f, *empty).status == TestStatus::Warn, "Selbsttest: wenig Platz = warn");
        f.set("storage.mounts", json::array({{{"mount", "/mnt/usb0"}, {"total", 9e11}, {"free_pct", 50.0}}}));
        CHECK(run("sw.storage.mounts", f, *empty).reason == "no_data_mount", "Selbsttest: ohne /data = skipped (no_data_mount)");

        Facts t;
        CHECK(run("hw.sensors.plausible", t, *empty).status == TestStatus::Skipped, "Selbsttest: Sensoren nicht lesbar = skipped");
        t.set("sys.temp.cpu_c", 40);
        t.set("sys.temp.soc_c", 38);
        CHECK(run("hw.sensors.plausible", t, *empty).status == TestStatus::Passed, "Selbsttest: Sensoren plausibel");
        t.set("sys.temp.cpu_c", 3);
        CHECK(run("hw.sensors.plausible", t, *empty).status == TestStatus::Warn, "Selbsttest: unplausibler Sensorwert = warn");

        Facts d;
        CHECK(run("sw.db.headers", d, *empty).status == TestStatus::Skipped, "Selbsttest: ohne Datenbanken = skipped");
        d.set("db.files", json::array({{{"readable", 1}, {"header_bad", 0}, {"truncated", 0}}, {{"readable", 1}, {"header_bad", 0}, {"truncated", 0}}}));
        CHECK(run("sw.db.headers", d, *empty).status == TestStatus::Passed, "Selbsttest: Datenbank-Koepfe ok");
        d.set("db.files", json::array({{{"readable", 1}, {"header_bad", 1}, {"truncated", 0}}, {{"readable", 0}}}));
        auto db = run("sw.db.headers", d, *empty);
        CHECK(db.status == TestStatus::Failed && measure(db, "bad") == 1 && measure(db, "unreadable") == 1, "Selbsttest: ungueltiger Kopf = failed");
        d.set("db.files", json::array({{{"readable", 0}}}));
        CHECK(run("sw.db.headers", d, *empty).status == TestStatus::Warn, "Selbsttest: nur unlesbare Datenbanken = warn, nicht passed");

        Facts s;
        s.set("payload.elf_files", json::array());
        s.set("smp.images", json::array());
        CHECK(run("sw.files.sanity", s, *empty).status == TestStatus::Skipped, "Selbsttest: keine Dateien zu pruefen = skipped");
        s.set("payload.elf_files", json::array({{{"path", "/a.elf"}, {"state", "ok"}}}));
        s.set("payload.broken_elfs", json::array());
        CHECK(run("sw.files.sanity", s, *empty).status == TestStatus::Passed, "Selbsttest: Dateien ok");
        s.set("smp.images", json::array({{{"path", "/x.exfat"}, {"truncated", 1}}}));
        CHECK(run("sw.files.sanity", s, *empty).status == TestStatus::Warn, "Selbsttest: abgeschnittenes Image = warn");
    }
    {  // Lesetest an einer echten (kleinen) Datei: i1_neg_complete hat ein 64-KiB-exFAT-Image
        Copy c("i1_neg_complete");
        auto p = make_snapshot_platform(c.dir.string(), false);
        const Facts f = collect_all(*p);
        auto r = run("hw.storage.read_throughput", f, *p);
        CHECK(r.status == TestStatus::Passed && measure(r, "bytes_read") == 65536 && measure(r, "mb_per_s") < 0, "Selbsttest: Lesetest liest die Datei, Durchsatz erst ab 8 MiB");
        TestContext stop;
        stop.cancelled = [] { return true; };
        auto cancelled = run("hw.storage.read_throughput", f, *p, stop);
        CHECK(cancelled.status == TestStatus::Cancelled && cancelled.reason == "cancelled", "Selbsttest: Abbruch = cancelled mit Grund, nie passed");
        // Datei nach dem Sammeln kuerzen: die Fakten nennen noch 64 KiB, gelesen werden aber nur 100 Byte
        const fs::path img = c.dir / "fs/data/homebrew/kopie.exfat";
        { std::ofstream(img, std::ios::binary | std::ios::trunc) << std::string(100, 'x'); }
        CHECK(run("hw.storage.read_throughput", f, *p).reason == "short_read" && run("hw.storage.read_throughput", f, *p).status == TestStatus::Failed, "Selbsttest: zu wenig gelesen = failed (short_read)");
        fs::remove(img);
        CHECK(run("hw.storage.read_throughput", f, *p).status == TestStatus::Failed, "Selbsttest: Lesefehler = failed");
        Facts none;
        CHECK(run("hw.storage.read_throughput", none, *p).reason == "no_file", "Selbsttest: ohne Datei = skipped (no_file)");
    }
    {  // Marker, Ergebnis, Fakten
        Sandbox w;
        auto rw = make_snapshot_platform(w.dir.string(), true);
        auto ro = make_snapshot_platform(w.dir.string(), false);
        CHECK(!selftest_begin(*ro, "20261111T100000Z", {"a"}) && !selftest_begin(*rw, "", {"a"}), "Selbsttest: ohne Schreibzugriff oder Uhr kein Marker");
        CHECK(selftest_begin(*rw, "20261111T100000Z", {"sw.db.headers", "hw.storage.read_throughput"}), "Selbsttest: Marker gesetzt");
        Facts f1;
        collect_selftest(*rw, f1);
        const json* st = f1.get("selftest.interrupted.started");
        CHECK(st && *st == "20261111T100000Z" && f1.get("selftest.interrupted.tests")->size() == 2, "Selbsttest: laufender Marker -> selftest.interrupted");

        TestResult ok, bad;
        ok.id = "sw.db.headers";
        ok.status = TestStatus::Passed;
        ok.measurements = {{"databases", 15, ""}};
        bad.id = "hw.storage.read_throughput";
        bad.status = TestStatus::Failed;
        bad.reason = "read_error";
        CHECK(selftest_finish(*rw, "20261111T100500Z", {ok, bad}, false), "Selbsttest: Ergebnis geschrieben");
        Facts f2;
        collect_selftest(*rw, f2);
        CHECK(!f2.get("selftest.interrupted.started"), "Selbsttest: nach dem Ende kein selftest.interrupted (Marker ist 'done', nicht geloescht)");
        CHECK(fs::exists(w.real(kSelftestMarkerPath)) && fs::exists(w.real(kSelftestLastPath)), "Selbsttest: Marker und Ergebnis bleiben liegen (nie loeschen)");
        const json* failed = f2.get("selftest.failed");
        CHECK(failed && failed->size() == 1 && (*failed)[0]["id"] == "hw.storage.read_throughput" && (*failed)[0]["reason"] == "read_error", "Selbsttest: selftest.failed");
        CHECK(*f2.get("test.sw.db.headers.status") == "passed" && f2.get("test.sw.db.headers.databases")->get<double>() == 15 && *f2.get("test.hw.storage.read_throughput.reason") == "read_error",
              "Selbsttest: test.<id>.*-Fakten");
        std::string fin;
        const auto back = load_last_selftest(rw->fs(), &fin);
        CHECK(back.size() == 2 && fin == "20261111T100500Z" && back[1].status == TestStatus::Failed && back[0].measurements.size() == 1, "Selbsttest: Ergebnis wird wieder gelesen");
        // zweiter Lauf ueberschreibt, loescht nichts
        CHECK(selftest_begin(*rw, "20261111T101000Z", {"a"}) && selftest_finish(*rw, "20261111T101100Z", {}, true), "Selbsttest: zweiter Lauf");
        Facts f3;
        collect_selftest(*rw, f3);
        CHECK(!f3.get("selftest.failed"), "Selbsttest: leeres Ergebnis (abgebrochen) meldet nichts als fehlgeschlagen");
    }
    {  // Bericht
        ReportInput in;
        in.created_utc = "20261111T120000Z";
        in.rules_version = "t";
        in.selftest_finished = "20261111T115500Z";
        TestResult a, b, c;
        a.id = "sw.db.headers";
        a.status = TestStatus::Passed;
        a.measurements = {{"databases", 15, ""}};
        b.id = "hw.storage.read_throughput";
        b.status = TestStatus::Failed;
        b.reason = "read_error";
        c.id = "hw.sensors.plausible";
        c.status = TestStatus::Skipped;
        c.reason = "sensors_unreadable";
        in.selftests = {a, b, c};
        const std::string de = build_report(in, ReportLang::De), en = build_report(in, ReportLang::En);
        auto has = [](const std::string& x, const std::string& n) { return x.find(n) != std::string::npos; };
        CHECK(has(de, "## Selbsttest") && has(de, "Letzter Lauf: 2026-11-11 11:55 UTC") && has(de, "Datenbanken der Systemsoftware (Kopf und Größe): bestanden, databases 15\n  Beweist nicht: Inhalt der Datenbanken"),
              "Selbsttest: Bericht DE (Status, Messwert, 'Beweist nicht')");
        CHECK(has(de, "Lesetest einer Image-Datei: fehlgeschlagen (read_error)") && has(de, "Temperatursensoren: übersprungen (sensors_unreadable)"), "Selbsttest: Bericht DE (fehlgeschlagen und uebersprungen)");
        CHECK(!has(de, "Lesetest einer Image-Datei: fehlgeschlagen (read_error)\n  Beweist nicht"), "Selbsttest: bei fehlgeschlagenen und uebersprungenen Tests kein 'Beweist nicht'");
        CHECK(has(en, "## Self-test") && has(en, "Read test of an image file: failed (read_error)") && has(en, "skipped (sensors_unreadable)"), "Selbsttest: Bericht EN");
        in.selftests.clear();
        CHECK(!has(build_report(in, ReportLang::De), "## Selbsttest"), "Selbsttest: ohne Lauf kein Abschnitt");
    }
}
