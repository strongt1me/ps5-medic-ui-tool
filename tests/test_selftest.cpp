// SPDX-License-Identifier: GPL-3.0-or-later
// Tests des Selbsttest-Frameworks (libmedic/src/selftest.cpp): Registry, Statusmodell, Marker, Fakten.
#include "check.hpp"
#include "testutil.hpp"
#include "medic/collectors.hpp"
#include "medic/fixes.hpp"
#include "medic/rules.hpp"
#include "medic/platform.hpp"
#include "medic/report.hpp"
#include "medic/selftest.hpp"
#include "medic/text_policy.hpp"

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace medic;
using nlohmann::json;

namespace {

using namespace testutil;

TestResult run(const char* id, const Facts& f, Platform& p, const TestContext& ctx = {}) {
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

// Plattform mit einstellbarer Temperaturfolge und Fehlerinjektion beim Schreiben, ueber einer echten (Sandbox-)Plattform.
struct ScriptedSys final : ISystemInfo {
    const ISystemInfo& inner;
    mutable std::vector<int> temps;  // Folge; der letzte Wert wiederholt sich; leer = nicht lesbar
    mutable std::size_t i = 0;
    explicit ScriptedSys(const ISystemInfo& in) : inner(in) {}
    std::string firmware() const override { return inner.firmware(); }
    std::string model() const override { return inner.model(); }
    std::vector<StorageInfo> storage() const override { return inner.storage(); }
    std::map<std::string, std::map<std::string, std::string>> components() const override { return inner.components(); }
    Thermal thermal() const override {
        Thermal t;
        if (temps.empty()) return t;
        t.cpu_c = temps[std::min(i, temps.size() - 1)];
        ++i;
        return t;
    }
};

struct ScriptedRw final : IFileSystemRW {
    IFileSystemRW* inner = nullptr;
    bool fail_write = false;
    bool fail_write_file = false;  // write_file scheitert (Budgetdatei)
    std::uint64_t corrupt_offset = ~0ull;  // Block, in dem ein Byte verfaelscht wird
    int write_calls = 0;
    bool write_file(const std::string& p, const std::string& c) override { return fail_write_file ? false : inner->write_file(p, c); }
    bool rename(const std::string& a, const std::string& b) override { return inner->rename(a, b); }
    bool make_dirs(const std::string& p) override { return inner->make_dirs(p); }
    bool remove_empty_dir(const std::string& p) override { return inner->remove_empty_dir(p); }
    bool append_file(const std::string& p, const std::string& c) override { return inner->append_file(p, c); }
    bool write_range(const std::string& p, std::uint64_t off, const std::string& c, bool sync) override {
        ++write_calls;
        if (fail_write) return false;
        if (off == corrupt_offset) {
            std::string bad = c;
            bad[100] = static_cast<char>(bad[100] ^ 0x5a);
            return inner->write_range(p, off, bad, sync);
        }
        return inner->write_range(p, off, c, sync);
    }
};

struct ScriptedPlatform final : Platform {
    std::unique_ptr<Platform> base;
    ScriptedSys sys_;
    ScriptedRw rw_;
    bool writable;
    ScriptedPlatform(std::unique_ptr<Platform> b, bool w) : base(std::move(b)), sys_(base->sys()), writable(w) { rw_.inner = base->fs_rw(); }
    const IFileSystem& fs() const override { return base->fs(); }
    const IProcessList& procs() const override { return base->procs(); }
    const ISystemInfo& sys() const override { return sys_; }
    IFileSystemRW* fs_rw() override { return writable ? &rw_ : nullptr; }
};

Facts data_mount(double free_bytes, double pct) {
    Facts f;
    f.set("storage.mounts", json::array({{{"mount", "/data"}, {"total", 9e11}, {"free", free_bytes}, {"free_pct", pct}}}));
    return f;
}

}  // namespace

void selftest_tests() {
    {  // Registry
        const auto& reg = selftest_registry();
        std::set<std::string> ids;
        bool order_ok = true, text_ok = true, risk_ok = true;
        int last_tier = 0;
        for (const auto& d : reg) {
            ids.insert(d.id);
            if (static_cast<int>(d.tier) < last_tier) order_ok = false;  // Schnell vor Standard vor Gruendlich
            last_tier = static_cast<int>(d.tier);
            text_ok = text_ok && !d.name_de.empty() && !d.name_en.empty() && !d.proves_not_de.empty() && !d.proves_not_en.empty() &&
                      find_system_update_advice(d.name_de + d.proves_not_de + d.name_en + d.proves_not_en).empty();
            risk_ok = risk_ok && (d.risk == "none" || d.risk == "read" || d.risk == "net_local" || d.risk == "load" || d.risk == "write");
            if (d.tier != TestTier::Thorough && d.risk != "none" && d.risk != "read" && d.risk != "net_local") risk_ok = false;  // Last und Schreiben nur in der Gruendlich-Stufe
        }
        CHECK(reg.size() >= 8 && ids.size() == reg.size() && order_ok && text_ok && risk_ok, "Selbsttest: Registry (eindeutige IDs, Stufen-Reihenfolge, Last/Schreiben nur Gruendlich, Texte)");
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
        // Review 13. Nov. 2026: ein exFAT-Image, dessen Kopf nicht lesbar oder ungueltig war (keine Volumengroesse), gilt nicht als geprueft
        s.set("smp.images", json::array({{{"path", "/x.exfat"}, {"ext", ".exfat"}, {"size", 4096}, {"tiny", 0}}}));
        CHECK(run("sw.files.sanity", s, *empty).status == TestStatus::Warn, "Selbsttest: exFAT-Image ohne lesbaren Kopf = warn, nicht passed");
        s.set("smp.images", json::array({{{"path", "/x.exfat"}, {"ext", ".exfat"}, {"size", 4096}, {"tiny", 0}, {"volume_bytes", 4096}, {"truncated", 0}}}));
        CHECK(run("sw.files.sanity", s, *empty).status == TestStatus::Passed, "Selbsttest: exFAT-Image mit lesbarem Kopf = passed");
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
        {   // Fix selftest_clear_marker (Marc, 24. Nov. 2026): Marker auf "done", Backup, Rueckgaengig bringt ihn zurueck
            const RuleSet rs = load_rules_from_file(kShipped);
            const Finding* fd = nullptr;
            const auto findings = evaluate(rs, f1);
            for (const auto& x : findings)
                if (x.rule_id == "selftest.interrupted") fd = &x;
            CHECK(fd && fd->fix == "selftest_clear_marker", "Selbsttest: selftest.interrupted hat den Fix selftest_clear_marker");
            if (fd) {
                const FixPlan plan = plan_fix(*fd, f1, rw->fs());
                CHECK(plan.changes.size() == 1 && plan.changes[0].kind == Change::Kind::WriteFile && plan.changes[0].path == kSelftestMarkerPath && plan.warnings.empty() && !plan.hint_en.empty(),
                      "Selbsttest: der Plan schreibt nur die Markierungsdatei");
                const ApplyResult ar = apply_plan(plan, *rw, policy_from_facts(f1), {"20261124T110000Z", true});
                CHECK(ar.ok, "Selbsttest: Fix angewendet (" + ar.error + ")");
                Facts after;
                collect_selftest(*rw, after);
                CHECK(!after.get("selftest.interrupted.started"), "Selbsttest: nach dem Fix kein selftest.interrupted mehr");
                std::string text;
                CHECK(rw->fs().read_file(kSelftestMarkerPath, text) && text.find("\"done\"") != std::string::npos && text.find("20261111T100000Z") != std::string::npos, "Selbsttest: Markierung steht auf done, der Start bleibt vermerkt");
                CHECK(plan_fix(*fd, after, rw->fs()).changes.empty(), "Selbsttest: ein zweiter Plan findet nichts mehr");
                const UndoResult ur = undo_txn(*rw, policy_from_facts(f1), ar.txn, "20261124T110100Z");
                Facts back;
                collect_selftest(*rw, back);
                CHECK(ur.ok && back.get("selftest.interrupted.started"), "Selbsttest: Rueckgaengig bringt die Markierung zurueck (" + ur.error + ")");
            }
        }

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
        CHECK(has(de, "## Selbsttest") && has(de, "Letzter Lauf: 11.11.2026 11:55 UTC") && has(de, "Datenbanken der Systemsoftware (Kopf und Größe): bestanden, databases 15\n  Beweist nicht: Inhalt der Datenbanken"),
              "Selbsttest: Bericht DE (Status, Messwert, 'Beweist nicht')");
        CHECK(has(de, "Lesetest einer Image-Datei: fehlgeschlagen (read_error)") && has(de, "Temperatursensoren: übersprungen (sensors_unreadable)"), "Selbsttest: Bericht DE (fehlgeschlagen und uebersprungen)");
        CHECK(!has(de, "Lesetest einer Image-Datei: fehlgeschlagen (read_error)\n  Beweist nicht"), "Selbsttest: bei fehlgeschlagenen und uebersprungenen Tests kein 'Beweist nicht'");
        CHECK(has(en, "## Self-test") && has(en, "Read test of an image file: failed (read_error)") && has(en, "skipped (sensors_unreadable)"), "Selbsttest: Bericht EN");
        in.selftests.clear();
        CHECK(!has(build_report(in, ReportLang::De), "## Selbsttest"), "Selbsttest: ohne Lauf kein Abschnitt");
    }

    {  // Waermetest (Gruendlich, Last): Abbruchbedingungen und Schutz
        TestContext ctx;
        ctx.thermal_burn_s = 1;
        ctx.thermal_cool_s = 1;
        ctx.step_ms = 40;
        auto thermal = [&](std::vector<int> temps, const TestContext& c = TestContext()) {
            Sandbox b;
            ScriptedPlatform sp(make_snapshot_platform(b.dir.string(), false), false);
            sp.sys_.temps = std::move(temps);
            const Facts none;
            TestContext cc = c;
            cc.thermal_burn_s = ctx.thermal_burn_s;
            cc.thermal_cool_s = ctx.thermal_cool_s;
            cc.step_ms = ctx.step_ms;
            return run("hw.thermal.short_soak", none, sp, cc);
        };
        auto ok = thermal({40});
        CHECK(ok.status == TestStatus::Passed && measure(ok, "rest_c") == 40 && measure(ok, "peak_c") == 40 && measure(ok, "rise_c") == 0 && measure(ok, "cores") >= 1,
              "Waermetest: gleichbleibende Temperatur = passed mit Messwerten");
        CHECK(thermal({}).reason == "sensors_unreadable" && thermal({}).status == TestStatus::Skipped, "Waermetest: ohne Sensor = skipped");
        auto warm = thermal({75});
        CHECK(warm.status == TestStatus::Skipped && warm.reason == "too_warm" && warm.measurements.empty(), "Waermetest: ueber 70 Grad in Ruhe startet nicht (skipped, keine Last)");
        auto hot = thermal({40, 41, 60, 90});
        CHECK(hot.status == TestStatus::Cancelled && hot.reason == "ceiling" && measure(hot, "peak_c") == 90, "Waermetest: 85 Grad = Abbruch (cancelled, ceiling), nie passed");
        auto rise = thermal({40, 41, 45, 50, 55, 62, 62, 62});
        CHECK(rise.status == TestStatus::Cancelled && rise.reason == "rise_rate", "Waermetest: zu schneller Anstieg = Abbruch (rise_rate)");
        std::vector<int> ramp;
        for (int i = 0; i < 40; ++i) ramp.push_back(std::min(60, 40 + 2 * i));
        auto slow = thermal(ramp);
        CHECK(slow.status == TestStatus::Warn && slow.reason == "slow_cooldown", "Waermetest: bleibt nach der Last deutlich ueber dem Ruhewert = warn (slow_cooldown)");
        // Abbruch durch den Nutzer: nach wenigen Schritten
        Sandbox b2;
        ScriptedPlatform sp2(make_snapshot_platform(b2.dir.string(), false), false);
        sp2.sys_.temps = {40};
        int calls = 0;
        TestContext stop = ctx;
        stop.cancelled = [&] { return ++calls > 3; };
        const Facts none;
        auto ab = run("hw.thermal.short_soak", none, sp2, stop);
        CHECK(ab.status == TestStatus::Cancelled && ab.reason == "cancelled", "Waermetest: Nutzerabbruch = cancelled");
    }
    {  // Der Waermetest blockiert die Hauptschleife des Dienstes nicht: jeder step() kehrt nach kurzer Zeit zurueck (frueher schlief er ein ganzes Intervall)
        Sandbox b3;
        ScriptedPlatform sp3(make_snapshot_platform(b3.dir.string(), false), false);
        sp3.sys_.temps = {40};
        TestContext c3;
        c3.thermal_burn_s = 1;
        c3.thermal_cool_s = 1;
        c3.step_ms = 600;
        const Facts none3;
        const TestDef* d3 = find_test("hw.thermal.short_soak");
        auto sess = d3 ? begin_selftest_session(sp3, none3, *d3, c3) : nullptr;
        CHECK(sess != nullptr, "Waermetest: Sitzung beginnt");
        double longest = 0;
        int steps = 0;
        if (sess) {
            for (; steps < 100000 && !sess->step(); ++steps) {
                static thread_local std::chrono::steady_clock::time_point last{};
                const auto now = std::chrono::steady_clock::now();
                if (steps > 0) longest = std::max(longest, std::chrono::duration<double, std::milli>(now - last).count());
                last = now;
            }
        }
        CHECK(sess && longest < 100, "Waermetest: kein einzelner step() dauert ein ganzes Messintervall (laengster Abstand " + std::to_string(longest) + " ms)");
        CHECK(sess && sess->result().status == TestStatus::Passed, "Waermetest: laeuft in kleinen Schritten durch und besteht");
    }
    {  // Steht der Hauptthread laenger als 3 s still (z. B. blockierendes send()), hoeren die Rechenthreads auf; der Test gilt dann als abgebrochen, nicht als "Leistungsabfall"
        Sandbox b4;
        ScriptedPlatform sp4(make_snapshot_platform(b4.dir.string(), false), false);
        sp4.sys_.temps = {40};
        TestContext c4;
        c4.thermal_burn_s = 20;
        c4.thermal_cool_s = 1;
        const Facts none4;
        const TestDef* d4 = find_test("hw.thermal.short_soak");
        auto sess4 = d4 ? begin_selftest_session(sp4, none4, *d4, c4) : nullptr;
        CHECK(sess4 != nullptr, "Waermetest (Stillstand): Sitzung beginnt");
        if (sess4) {
            sess4->step();  // startet die Rechenthreads
            std::this_thread::sleep_for(std::chrono::milliseconds(4500));
            bool done = false;
            for (int i = 0; i < 50 && !done; ++i) done = sess4->step();
            CHECK(done && sess4->result().status == TestStatus::Cancelled && sess4->result().reason == "stalled", "Waermetest: Stillstand des Hauptthreads = abgebrochen (stalled), kein Befund");
        }
    }
    {  // Schreibtest (Gruendlich): Schutzregeln, Pruefmuster, nie loeschen
        TestContext ctx;
        ctx.write_bytes = 8u << 20;  // zwei Bloecke
        ctx.today = "20261111";
        const Facts mount = data_mount(5e11, 50.0);
        Sandbox w;
        ScriptedPlatform sp(make_snapshot_platform(w.dir.string(), true), true);
        auto r1 = run("hw.storage.write_verify", mount, sp, ctx);
        CHECK(r1.status == TestStatus::Passed && measure(r1, "bytes_written") == 8u << 20 && measure(r1, "bad_blocks") == 0, "Schreibtest: schreiben, zuruecklesen, vergleichen = passed");
        CHECK(fs::file_size(w.real(kSelftestWritePath)) == (8u << 20), "Schreibtest: die EINE Testdatei hat die vorgesehene Groesse");
        const json budget = json::parse(slurp(w.real(kSelftestBudgetPath)));
        CHECK(budget["last_date"] == "20261111" && budget["runs"] == 1 && budget["total_bytes"] == (8u << 20), "Schreibtest: Budget fortgeschrieben");
        auto again = run("hw.storage.write_verify", mount, sp, ctx);
        CHECK(again.status == TestStatus::Skipped && again.reason == "done_today", "Schreibtest: hoechstens ein Lauf je Tag (skipped, done_today)");
        ctx.today = "20261112";
        auto next = run("hw.storage.write_verify", mount, sp, ctx);
        CHECK(next.status == TestStatus::Passed && fs::exists(w.real(kSelftestWritePath)), "Schreibtest: am naechsten Tag wieder, dieselbe Datei wird ueberschrieben, nie geloescht");
        CHECK(json::parse(slurp(w.real(kSelftestBudgetPath)))["runs"] == 2, "Schreibtest: zweiter Lauf im Budget");

        // Schutzregeln: jede fehlende Vorbedingung = skipped
        Sandbox ro;
        ScriptedPlatform spro(make_snapshot_platform(ro.dir.string(), false), false);
        CHECK(run("hw.storage.write_verify", mount, spro, ctx).reason == "no_write", "Schreibtest: ohne Schreibzugriff = skipped");
        TestContext noclock = ctx;
        noclock.today.clear();
        Sandbox c1;
        ScriptedPlatform spc(make_snapshot_platform(c1.dir.string(), true), true);
        CHECK(run("hw.storage.write_verify", mount, spc, noclock).reason == "no_clock", "Schreibtest: ohne gueltige Uhr = skipped");
        CHECK(run("hw.storage.write_verify", data_mount(5e11, 5.0), spc, ctx).reason == "low_space", "Schreibtest: unter 10 % frei = skipped (low_space)");
        CHECK(run("hw.storage.write_verify", data_mount(1e8, 50.0), spc, ctx).reason == "low_space", "Schreibtest: weniger als Dateigroesse plus 1 GiB frei = skipped");
        CHECK(run("hw.storage.write_verify", Facts(), spc, ctx).reason == "no_data_mount", "Schreibtest: ohne /data = skipped");
        CHECK(!fs::exists(c1.real(kSelftestWritePath)), "Schreibtest: bei fehlender Vorbedingung wird nichts geschrieben");
        Sandbox bb;
        spit(bb.real(kSelftestBudgetPath), "{\"last_date\":\"20260101\",\"runs\":9,\"total_bytes\":53687091200}\n");
        ScriptedPlatform spb(make_snapshot_platform(bb.dir.string(), true), true);
        CHECK(run("hw.storage.write_verify", mount, spb, ctx).reason == "budget_used", "Schreibtest: Gesamtbudget (50 GiB) aufgebraucht = skipped");

        // Fehler: verfaelschter Block, Schreibfehler, Abbruch
        Sandbox m;
        ScriptedPlatform spm(make_snapshot_platform(m.dir.string(), true), true);
        spm.rw_.corrupt_offset = 4u << 20;
        auto bad = run("hw.storage.write_verify", mount, spm, ctx);
        CHECK(bad.status == TestStatus::Failed && bad.reason == "verify_mismatch" && measure(bad, "bad_blocks") == 1 && measure(bad, "first_bad_offset") == (4u << 20), "Schreibtest: abweichender Block = failed (verify_mismatch) mit Offset");
        Sandbox e;
        ScriptedPlatform spe(make_snapshot_platform(e.dir.string(), true), true);
        spe.rw_.fail_write = true;
        auto werr = run("hw.storage.write_verify", mount, spe, ctx);
        {
            // nichts geschrieben: die Tagesmarke vom Testbeginn wird zurueckgenommen, es ist kein Budget verbraucht
            const std::string bt = fs::exists(e.real(kSelftestBudgetPath)) ? slurp(e.real(kSelftestBudgetPath)) : std::string("{}");
            const json bj = json::parse(bt, nullptr, false);
            CHECK(werr.status == TestStatus::Failed && werr.reason == "write_error" && bj.is_object() && bj.value("last_date", std::string()) != "20261111" && bj.value("total_bytes", std::uint64_t{0}) == 0,
                  "Schreibtest: Schreibfehler = failed, kein Budget verbraucht und keine Tagesmarke (nichts geschrieben)");
        }
        Sandbox nb;
        ScriptedPlatform spnb(make_snapshot_platform(nb.dir.string(), true), true);
        spnb.rw_.fail_write_file = true;
        auto nbr = run("hw.storage.write_verify", mount, spnb, ctx);
        CHECK(nbr.status == TestStatus::Skipped && nbr.reason == "budget_unwritable" && spnb.rw_.write_calls == 0, "Schreibtest: Budget nicht schreibbar = skipped (budget_unwritable), es wird nichts geschrieben");
        Sandbox a;
        ScriptedPlatform spa(make_snapshot_platform(a.dir.string(), true), true);
        int n = 0;
        TestContext stop = ctx;
        stop.cancelled = [&] { return ++n > 1; };
        auto cancelled = run("hw.storage.write_verify", mount, spa, stop);
        CHECK(cancelled.status == TestStatus::Cancelled && fs::exists(a.real(kSelftestBudgetPath)), "Schreibtest: Abbruch = cancelled, das Budget zaehlt das Geschriebene");
    }
    {  // Rechenfehler unter Last und Speicher-Muster-Test
        TestContext ctx;
        ctx.thermal_burn_s = 1;
        ctx.thermal_cool_s = 1;
        ctx.step_ms = 40;
        Sandbox b;
        ScriptedPlatform sp(make_snapshot_platform(b.dir.string(), false), false);
        sp.sys_.temps = {40};
        const Facts none;
        auto clean = run("hw.thermal.short_soak", none, sp, ctx);
        CHECK(clean.status == TestStatus::Passed && measure(clean, "compute_errors") == 0, "Waermetest: ohne Fehler compute_errors 0");
        TestContext fault = ctx;
        fault.inject_compute_fault = true;
        Sandbox b2;
        ScriptedPlatform sp2(make_snapshot_platform(b2.dir.string(), false), false);
        sp2.sys_.temps = {40};
        auto bad = run("hw.thermal.short_soak", none, sp2, fault);
        CHECK(bad.status == TestStatus::Failed && bad.reason == "compute_mismatch" && measure(bad, "compute_errors") >= 1, "Waermetest: abweichendes Rechenergebnis = failed (compute_mismatch)");

        TestContext mem;
        mem.mem_bytes = 16u << 20;  // ein Block
        auto m1 = run("hw.mem.userland_pattern", none, sp, mem);
        CHECK(m1.status == TestStatus::Passed && measure(m1, "bytes_tested") == (16u << 20) && measure(m1, "patterns") == 6 && measure(m1, "bad_words") == 0, "Speichertest: sechs Muster, keine Abweichung = passed");
        mem.inject_mem_fault = true;
        auto m2 = run("hw.mem.userland_pattern", none, sp, mem);
        CHECK(m2.status == TestStatus::Failed && m2.reason == "mem_mismatch" && measure(m2, "bad_words") == 1 && measure(m2, "first_bad_offset") == 24, "Speichertest: gekipptes Bit = failed (mem_mismatch) mit Offset");
        TestContext stop;
        stop.mem_bytes = 32u << 20;
        int n = 0;
        stop.cancelled = [&] { return ++n > 2; };
        CHECK(run("hw.mem.userland_pattern", none, sp, stop).status == TestStatus::Cancelled, "Speichertest: Abbruch = cancelled");
    }
    {  // Nutzer-Test eintragen
        Sandbox w;
        auto rw = make_snapshot_platform(w.dir.string(), true);
        TestResult a;
        a.id = "hw.display.pattern";
        a.status = TestStatus::Warn;
        a.reason = "user_reported";
        CHECK(selftest_record(*rw, "20261111T120000Z", a), "Nutzer-Test: Eintrag ohne vorhandenes Ergebnis");
        TestResult ok;
        ok.id = "sw.db.headers";
        ok.status = TestStatus::Passed;
        CHECK(selftest_finish(*rw, "20261111T121000Z", {ok}, false) && selftest_record(*rw, "20261111T122000Z", a), "Nutzer-Test: Eintrag neben vorhandenen Ergebnissen");
        TestResult b = a;
        b.status = TestStatus::Passed;
        b.reason = "";
        CHECK(selftest_record(*rw, "20261111T123000Z", b), "Nutzer-Test: Eintrag ersetzt den gleichen Test");
        std::string fin;
        const auto all = load_last_selftest(rw->fs(), &fin);
        int display = 0;
        for (const auto& x : all) display += x.id == "hw.display.pattern" ? 1 : 0;
        CHECK(all.size() == 2 && display == 1 && fin == "20261111T121000Z" && all[1].status == TestStatus::Passed, "Nutzer-Test: genau ein Eintrag je Test, Lauf-Zeit bleibt");
        const auto sess = begin_selftest_session(*rw, Facts(), *find_test("hw.pad.buttons"));
        CHECK(sess->step() && sess->result().status == TestStatus::Skipped && sess->result().reason == "needs_user", "Nutzer-Test: im Ablauf nur skipped (needs_user), nie bestanden");
    }
    {  // Luefterregler: Faktum aus config.json, im Waermetest genannt
        Copy c("f1_fan_controller");
        auto p = make_snapshot_platform(c.dir.string(), false);
        const Facts f = collect_all(*p);
        CHECK(f.get("fan.controller.name") && *f.get("fan.controller.name") == "PS5 Cooling Center" && *f.get("fan.controller.mode") == "automatic" && f.get("fan.controller.target_c")->get<double>() == 68, "Luefterregler: Fakten aus der config.json");
        Sandbox b;
        ScriptedPlatform sp(make_snapshot_platform(b.dir.string(), false), false);
        sp.sys_.temps = {40};
        TestContext ctx;
        ctx.thermal_burn_s = 1;
        ctx.thermal_cool_s = 1;
        ctx.step_ms = 40;
        auto r = run("hw.thermal.short_soak", f, sp, ctx);
        CHECK(r.status == TestStatus::Passed && r.reason == "fan_controller" && measure(r, "fan_controller_target_c") == 68, "Waermetest: ohne Anstieg und mit Luefterregler = passed mit Grund fan_controller");
        Sandbox b2;
        ScriptedPlatform sp2(make_snapshot_platform(b2.dir.string(), false), false);
        sp2.sys_.temps = {40};
        auto r2 = run("hw.thermal.short_soak", Facts(), sp2, ctx);
        CHECK(r2.status == TestStatus::Passed && r2.reason == "no_heating", "Waermetest: ohne Anstieg und ohne Luefterregler = Grund no_heating");
        CHECK(measure(r2, "cpus_online") >= 1 && measure(r2, "threads_started") >= 1 && measure(r2, "threads_started") <= measure(r2, "cpus_online") && measure(r2, "steps") >= 2 && measure(r2, "max_step_gap_ms") >= 0 &&
                  measure(r2, "rounds_min_thread") >= 0 && measure(r2, "rounds_max_thread") >= measure(r2, "rounds_min_thread"),
              "Waermetest: Diagnose-Messwerte (Kernzahl, Threads, Schritte, laengste Pause, Runden je Thread) stehen im Ergebnis");
    }
    {  // Kernel-Log sammeln (D10b): anhaengen, Doppelte, Neustart rotiert, nie loeschen
        Sandbox w;
        std::unique_ptr<Platform> rw;  // die Plattform liest system.json und klog.txt beim Anlegen: nach jeder Aenderung neu anlegen
        auto sys = [&](long long boot) {
            spit(w.dir / "system.json", R"({"fw":"12.00","model":"T","storage":[],"components":{},"boot":)" + std::to_string(boot) + "}");
            rw = make_snapshot_platform(w.dir.string(), true);
        };
        auto klog = [&](const std::string& text) {
            spit(w.dir / "klog.txt", text);
            rw = make_snapshot_platform(w.dir.string(), true);
        };
        const std::string l1 = "[kstuff.elf] patch_shellui failed", l2 = "[KERNEL] WARNING: etwas";
        sys(1794000000);
        klog("<118>" + l1 + "\n");
        const Facts a = collect_all(*rw);
        CHECK(persist_klog(*rw, a), "klog sammeln: erste Zeilen werden angehaengt");
        const std::string text1 = slurp(w.real(kKlogBootPath));
        CHECK(text1 == "# boot 1794000000\n" + l1 + "\n", "klog sammeln: Kopfzeile mit Startzeit, danach die Zeile");
        CHECK(!persist_klog(*rw, collect_all(*rw)), "klog sammeln: nichts Neues = nichts geschrieben");
        klog("<118>" + l1 + "\n<118>" + l2 + "\n");
        CHECK(persist_klog(*rw, collect_all(*rw)) && slurp(w.real(kKlogBootPath)) == "# boot 1794000000\n" + l1 + "\n" + l2 + "\n", "klog sammeln: neue Zeile angehaengt, Doppelte nicht");
        klog("");  // spaeterer Check-up: der Server liefert nichts mehr, die Datei enthaelt noch alles
        const Facts later = collect_all(*rw);
        CHECK(later.get("klog.persisted_lines")->get<long>() == 2 && later.get("klog.lines")->size() == 2 && later.get("klog.new_lines")->empty(), "klog sammeln: spaeterer Check-up sieht die gesammelten Zeilen");
        // neuer Konsolenstart: alte Datei beiseite, neue beginnt
        sys(1794999999);
        klog("<118>[kstuff.elf] neu failed\n");
        const Facts boot2 = collect_all(*rw);
        CHECK(boot2.get("klog.persisted_lines")->get<long>() == 0, "klog sammeln: Zeilen eines frueheren Starts zaehlen nicht");
        CHECK(persist_klog(*rw, boot2), "klog sammeln: neuer Start schreibt");
        CHECK(slurp(w.real("/data/ps5medic/klog_boot_1794000000.txt")) == text1 + l2 + "\n" && slurp(w.real(kKlogBootPath)) == "# boot 1794999999\n[kstuff.elf] neu failed\n",
              "klog sammeln: alte Datei verschoben (nie geloescht), neue begonnen");
        // Schutz
        auto ro = make_snapshot_platform(w.dir.string(), false);
        CHECK(!persist_klog(*ro, boot2), "klog sammeln: ohne Schreibzugriff nichts");
        sys(0);
        CHECK(!persist_klog(*rw, collect_all(*rw)), "klog sammeln: ohne bekannte Startzeit nichts");
        sys(1794999999);
        spit(w.real(kKlogBootPath), "fremder Inhalt\n");
        klog("<118>[kstuff.elf] noch eine failed\n");
        CHECK(!persist_klog(*rw, collect_all(*rw)) && slurp(w.real(kKlogBootPath)) == "fremder Inhalt\n", "klog sammeln: fremde Datei gleichen Namens bleibt unangetastet");
    }
    {  // Lokaler Netztest (Standard): nur das Gateway im eigenen Netz, nie das Internet
        auto net = [&](const std::string& gateway, const std::string& probe) {
            Sandbox w;
            std::string sys = R"({"fw":"12.00","model":"T","storage":[])";
            if (!gateway.empty()) sys += R"(,"gateway":")" + gateway + R"(")";
            if (!probe.empty()) sys += R"(,"net_probe":)" + probe;
            spit(w.dir / "system.json", sys + "}");
            auto p = make_snapshot_platform(w.dir.string(), false);
            const Facts f = collect_all(*p);
            TestContext ctx;
            ctx.net_gap_ms = 0;
            return std::make_pair(run("sw.net.local", f, *p, ctx), f);
        };
        {
            const auto [r, f] = net("192.168.1.1", R"({"reply":true,"rtt_ms":3.0})");
            CHECK(f.get("net.gateway") && f.get("net.gateway")->get<std::string>() == "192.168.1.1", "Netztest: Gateway wird als Fakt gesammelt");
            CHECK(r.status == TestStatus::Passed && measure(r, "loss_pct") == 0 && measure(r, "probes") == 20 && std::abs(measure(r, "rtt_avg_ms") - 3.0) < 1e-9, "Netztest: antwortendes Gateway = passed, Messwerte");
        }
        {
            const auto [r, f] = net("192.168.1.1", R"({"reply":true,"rtt_ms":3.0,"loss_every":3})");
            CHECK(r.status == TestStatus::Warn && r.reason == "packet_loss" && measure(r, "loss_pct") > 20, "Netztest: jeder dritte Versuch verloren = warn packet_loss");
        }
        {
            const auto [r, f] = net("10.0.0.1", R"({"reply":true,"rtt_ms":250.0})");
            CHECK(r.status == TestStatus::Warn && r.reason == "slow_gateway", "Netztest: langsame Antwort = warn slow_gateway");
        }
        {
            const auto [r, f] = net("192.168.1.1", R"({"reply":false})");
            CHECK(r.status == TestStatus::Skipped && r.reason == "no_response", "Netztest: schweigender Router = skipped, nie failed");
        }
        {
            const auto [r, f] = net("192.168.1.1", "");
            CHECK(r.status == TestStatus::Skipped && r.reason == "probe_error", "Netztest: Sonde nicht moeglich = skipped probe_error");
        }
        {
            const auto [r, f] = net("", R"({"reply":true})");
            CHECK(r.status == TestStatus::Skipped && r.reason == "no_gateway", "Netztest: ohne Gateway = skipped");
        }
        for (const char* gw : {"8.8.8.4", "1.1.1.1", "172.32.0.1", "172.15.0.1", "192.169.0.1", "169.255.0.1", "192.168.1.1x", "kein.ip", "300.1.1.1"}) {
            const auto [r, f] = net(gw, R"({"reply":true})");
            CHECK(r.status == TestStatus::Skipped && r.reason == "not_local", std::string("Netztest: Gateway ausserhalb des eigenen Netzes wird nie angesprochen: ") + gw);
        }
        for (const char* gw : {"10.1.2.3", "172.16.0.1", "172.31.255.254", "192.168.0.254", "169.254.1.1"}) {
            const auto [r, f] = net(gw, R"({"reply":true})");
            CHECK(r.status == TestStatus::Passed, std::string("Netztest: privates Gateway wird angesprochen: ") + gw);
        }
        {  // Abbruch wirkt
            Sandbox w;
            spit(w.dir / "system.json", R"({"fw":"12.00","model":"T","storage":[],"gateway":"192.168.1.1","net_probe":{"reply":true}})");
            auto p = make_snapshot_platform(w.dir.string(), false);
            const Facts f = collect_all(*p);
            TestContext ctx;
            ctx.net_gap_ms = 0;
            auto s = begin_selftest_session(*p, f, *find_test("sw.net.local"), ctx);
            s->step();
            s->abort("user");
            CHECK(s->step() && s->result().status == TestStatus::Cancelled, "Netztest: Abbruch = cancelled");
        }
    }
}
