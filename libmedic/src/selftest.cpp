// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/selftest.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

namespace medic {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

double seconds_since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

TestResult make_result(const TestDef& def, TestStatus s, const std::string& reason = {}) {
    TestResult r;
    r.id = def.id;
    r.status = s;
    r.reason = reason;
    return r;
}

double num_fact(const Facts& f, const char* path, double fallback = -1) {
    const json* v = f.get(path);
    return v && v->is_number() ? v->get<double>() : fallback;
}

// ---- Schnelltests (nur Fakten lesen) ------------------------------------------------------------------------------------------------

// Der Speicher unter /data muss lesbar sein und Platz haben. Die Schwelle entspricht der Regel storage.low_space_data (5 %).
TestResult test_storage_mounts(const Platform&, const Facts& f, const TestDef& d, const TestContext&) {
    const json* mounts = f.get("storage.mounts");
    if (!mounts || !mounts->is_array()) return make_result(d, TestStatus::Skipped, "storage_unreadable");
    for (const auto& m : *mounts) {
        if (!m.is_object() || m.value("mount", std::string()) != "/data" || !m.contains("total") || !m["total"].is_number() || m["total"].get<double>() <= 0) continue;
        const double pct = m.value("free_pct", -1.0);
        TestResult r = make_result(d, pct < 5.0 ? TestStatus::Warn : TestStatus::Passed, pct < 5.0 ? "low_space" : "");
        r.measurements.push_back({"free_pct", pct, "%"});
        r.measurements.push_back({"total_gb", m["total"].get<double>() / 1e9, "GB"});
        return r;
    }
    return make_result(d, TestStatus::Skipped, "no_data_mount");
}

// Temperaturen muessen lesbar und plausibel sein (10 bis 100 Grad). Ein ausserhalb liegender Wert ist ein Warnhinweis, kein Urteil ueber die Hardware.
TestResult test_sensors(const Platform&, const Facts& f, const TestDef& d, const TestContext&) {
    const double cpu = num_fact(f, "sys.temp.cpu_c"), soc = num_fact(f, "sys.temp.soc_c");
    if (cpu < 0 && soc < 0) return make_result(d, TestStatus::Skipped, "sensors_unreadable");
    TestResult r = make_result(d, TestStatus::Passed);
    for (const auto& [name, v] : {std::pair<const char*, double>{"cpu_c", cpu}, {"soc_c", soc}}) {
        if (v < 0) continue;
        r.measurements.push_back({name, v, "C"});
        if (v < 10 || v > 100) {
            r.status = TestStatus::Warn;
            r.reason = "implausible";
        }
    }
    return r;
}

// Datenbanken der Systemsoftware: Kopf und Groesse (die tiefe Pruefung steht in db.integrity_failed). Ungueltiger Kopf oder abgeschnitten = failed.
TestResult test_db_headers(const Platform&, const Facts& f, const TestDef& d, const TestContext&) {
    const json* files = f.get("db.files");
    if (!files || !files->is_array() || files->empty()) return make_result(d, TestStatus::Skipped, "no_databases");
    int total = 0, bad = 0, unreadable = 0;
    for (const auto& x : *files) {
        if (!x.is_object()) continue;
        ++total;
        if (x.value("readable", 1) != 1) {
            ++unreadable;
            continue;
        }
        if (x.value("header_bad", 0) == 1 || x.value("truncated", 0) == 1) ++bad;
    }
    TestResult r = make_result(d, bad ? TestStatus::Failed : (unreadable ? TestStatus::Warn : TestStatus::Passed), bad ? "header_or_size" : (unreadable ? "unreadable" : ""));
    r.measurements.push_back({"databases", static_cast<double>(total), ""});
    r.measurements.push_back({"bad", static_cast<double>(bad), ""});
    r.measurements.push_back({"unreadable", static_cast<double>(unreadable), ""});
    return r;
}

// Dateien, die Medic ohne Entschluesseln beurteilen kann: Payload-ELF, exFAT-Images, eboot.bin. Auffaelliges = warn (die Befunde nennen die Dateien).
TestResult test_files_sanity(const Platform&, const Facts& f, const TestDef& d, const TestContext&) {
    const json* elfs = f.get("payload.elf_files");
    const json* images = f.get("smp.images");
    const bool no_elfs = !elfs || !elfs->is_array() || elfs->empty(), no_images = !images || !images->is_array() || images->empty();
    if (no_elfs && no_images) return make_result(d, TestStatus::Skipped, "no_file_data");  // nichts zu pruefen heisst nicht "bestanden"
    auto count = [&](const char* path) {
        const json* a = f.get(path);
        return a && a->is_array() ? static_cast<double>(a->size()) : 0.0;
    };
    double bad_images = 0, checked_images = 0;
    if (images && images->is_array())
        for (const auto& img : *images) {
            if (!img.is_object()) continue;
            ++checked_images;
            if (img.value("truncated", 0) == 1 || img.value("tiny", 0) == 1) ++bad_images;
        }
    const double broken = count("payload.broken_elfs"), eboot = count("games.eboot_problems");
    TestResult r = make_result(d, broken + eboot + bad_images > 0 ? TestStatus::Warn : TestStatus::Passed, broken + eboot + bad_images > 0 ? "findings" : "");
    r.measurements.push_back({"elf_files", count("payload.elf_files"), ""});
    r.measurements.push_back({"images", checked_images, ""});
    r.measurements.push_back({"problems", broken + eboot + bad_images, ""});
    return r;
}

// ---- Standardtest: Lesedurchsatz ----------------------------------------------------------------------------------------------------

// Liest bis zu 128 MiB der groessten vorhandenen Image-Datei (Bloecke zu 1 MiB, hoechstens 15 s) und misst den Durchsatz. Der Inhalt wird nicht
// ausgewertet. EIO oder zu wenig Daten = failed. Ohne Baseline urteilt der Test nicht ueber die Zahl, er speichert sie.
TestResult test_read_throughput(const Platform& p, const Facts& f, const TestDef& d, const TestContext& ctx) {
    const json* images = f.get("smp.images");
    std::string best;
    std::uint64_t best_size = 0;
    if (images && images->is_array())
        for (const auto& img : *images) {
            if (!img.is_object() || !img.contains("path") || !img["path"].is_string() || !img.contains("size") || !img["size"].is_number()) continue;
            const std::uint64_t sz = img["size"].get<std::uint64_t>();
            if (sz >= (64u << 10) && sz > best_size) {
                best_size = sz;
                best = img["path"].get<std::string>();
            }
        }
    if (best.empty()) return make_result(d, TestStatus::Skipped, "no_file");

    const std::uint64_t want = std::min(best_size, kReadTestMaxBytes);
    constexpr std::size_t kBlock = 1u << 20;
    std::uint64_t got = 0;
    const auto t0 = Clock::now();
    std::string reason;
    TestStatus status = TestStatus::Passed;
    while (got < want) {
        if (ctx.cancelled && ctx.cancelled()) {
            status = TestStatus::Cancelled;
            reason = "cancelled";
            break;
        }
        if (seconds_since(t0) > kReadTestMaxSeconds) {
            reason = "time_limit";  // nicht schlimm: gemessen wird, was in der Zeit gelesen wurde
            break;
        }
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(kBlock, want - got));
        std::string chunk;
        if (!p.fs().read_range(best, got, n, chunk)) {
            status = TestStatus::Failed;
            reason = "read_error";
            break;
        }
        if (chunk.size() < n) {
            status = TestStatus::Failed;
            reason = "short_read";
            got += chunk.size();
            break;
        }
        got += chunk.size();
    }
    const double secs = std::max(seconds_since(t0), 1e-6);
    TestResult r = make_result(d, status, reason);
    r.measurements.push_back({"bytes_read", static_cast<double>(got), "B"});
    r.measurements.push_back({"seconds", secs, "s"});
    if (status == TestStatus::Passed && got >= (8u << 20)) r.measurements.push_back({"mb_per_s", static_cast<double>(got) / 1e6 / secs, "MB/s"});  // erst ab 8 MiB aussagekraeftig
    return r;
}

using TestFn = TestResult (*)(const Platform&, const Facts&, const TestDef&, const TestContext&);
struct Entry {
    TestDef def;
    TestFn fn;
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> list = {
        {{"sw.storage.mounts", TestTier::Quick, "none", 1, "Speicher unter /data", "Storage under /data", "Zustand der Hardware, Verschleiß", "hardware condition, wear"}, test_storage_mounts},
        {{"hw.sensors.plausible", TestTier::Quick, "none", 1, "Temperatursensoren", "Temperature sensors", "Kühlleistung, Lüfter, Verhalten unter Last", "cooling performance, fan, behaviour under load"}, test_sensors},
        {{"sw.db.headers", TestTier::Quick, "none", 2, "Datenbanken der Systemsoftware (Kopf und Größe)", "System software databases (header and size)", "Inhalt der Datenbanken (siehe Datenbank-Prüfung)", "database content (see the database check)"}, test_db_headers},
        {{"sw.files.sanity", TestTier::Quick, "none", 2, "Payloads, Images und eboot.bin (Kopf)", "Payloads, images and eboot.bin (header)", "Inhalt der Dateien, Entschlüsseln, Start des Spiels", "file contents, decryption, whether a game starts"}, test_files_sanity},
        {{"hw.storage.read_throughput", TestTier::Standard, "read", 20, "Lesetest einer Image-Datei", "Read test of an image file", "Schreibleistung, Verschleiß, ungelesene Bereiche", "write speed, wear, unread areas"}, test_read_throughput},
    };
    return list;
}

std::string status_text(TestStatus s) { return to_string(s); }

json result_json(const TestResult& r) {
    json m = json::array();
    for (const auto& x : r.measurements) m.push_back({{"name", x.name}, {"value", x.value}, {"unit", x.unit}});
    return {{"id", r.id}, {"status", status_text(r.status)}, {"reason", r.reason}, {"started", r.started}, {"duration_s", r.duration_s}, {"measurements", m}};
}

bool valid_ts(const std::string& ts) {
    return !ts.empty() && ts.size() <= 40 && std::all_of(ts.begin(), ts.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

}  // namespace

const char* to_string(TestStatus s) {
    switch (s) {
        case TestStatus::NotRun: return "not_run";
        case TestStatus::Passed: return "passed";
        case TestStatus::Warn: return "warn";
        case TestStatus::Failed: return "failed";
        case TestStatus::Error: return "error";
        case TestStatus::Cancelled: return "cancelled";
        case TestStatus::Skipped: return "skipped";
    }
    return "not_run";
}

TestStatus test_status_from_string(const std::string& s) {
    for (TestStatus x : {TestStatus::Passed, TestStatus::Warn, TestStatus::Failed, TestStatus::Error, TestStatus::Cancelled, TestStatus::Skipped})
        if (s == to_string(x)) return x;
    return TestStatus::NotRun;
}

const std::vector<TestDef>& selftest_registry() {
    static const std::vector<TestDef> defs = [] {
        std::vector<TestDef> d;
        for (const auto& e : entries()) d.push_back(e.def);
        return d;
    }();
    return defs;
}

const TestDef* find_test(const std::string& id) {
    for (const auto& d : selftest_registry())
        if (d.id == id) return &d;
    return nullptr;
}

TestResult run_selftest(const Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx) {
    for (const auto& e : entries()) {
        if (e.def.id != def.id) continue;
        const auto t0 = Clock::now();
        TestResult r;
        try {
            r = e.fn(p, facts, e.def, ctx);
        } catch (const std::exception&) {
            r = make_result(e.def, TestStatus::Error, "exception");  // ein Fehler im Test ist nie "passed"
        }
        r.id = e.def.id;
        r.duration_s = seconds_since(t0);
        return r;
    }
    return make_result(def, TestStatus::Error, "unknown_test");
}

bool selftest_begin(Platform& p, const std::string& ts, const std::vector<std::string>& ids) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw || !valid_ts(ts)) return false;
    return rw->write_file(kSelftestMarkerPath, json({{"state", "running"}, {"started", ts}, {"tests", ids}}).dump() + "\n");
}

bool selftest_finish(Platform& p, const std::string& ts, const std::vector<TestResult>& results, bool cancelled) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw || !valid_ts(ts)) return false;
    json arr = json::array();
    for (const auto& r : results) arr.push_back(result_json(r));
    const bool ok1 = rw->write_file(kSelftestLastPath, json({{"finished", ts}, {"cancelled", cancelled}, {"results", arr}}).dump(2) + "\n");
    // Marker nicht loeschen, sondern auf "done" setzen: ein erhaltenes "running" heisst, der Lauf wurde nicht ordentlich beendet.
    const bool ok2 = rw->write_file(kSelftestMarkerPath, json({{"state", "done"}, {"finished", ts}}).dump() + "\n");
    return ok1 && ok2;
}

std::vector<TestResult> load_last_selftest(const IFileSystem& fs, std::string* finished_ts) {
    std::vector<TestResult> out;
    std::string text;
    if (!fs.read_file(kSelftestLastPath, text)) return out;
    const json j = json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("results") || !j["results"].is_array()) return out;
    if (finished_ts && j.contains("finished") && j["finished"].is_string()) *finished_ts = j["finished"].get<std::string>();
    for (const auto& x : j["results"]) {
        if (!x.is_object() || !x.contains("id") || !x["id"].is_string()) continue;
        TestResult r;
        r.id = x["id"].get<std::string>();
        r.status = test_status_from_string(x.value("status", std::string()));
        r.reason = x.value("reason", std::string());
        r.started = x.value("started", std::string());
        r.duration_s = x.value("duration_s", 0.0);
        if (x.contains("measurements") && x["measurements"].is_array())
            for (const auto& m : x["measurements"])
                if (m.is_object() && m.contains("name") && m["name"].is_string() && m.contains("value") && m["value"].is_number())
                    r.measurements.push_back({m["name"].get<std::string>(), m["value"].get<double>(), m.value("unit", std::string())});
        out.push_back(std::move(r));
    }
    return out;
}

void collect_selftest(const Platform& p, Facts& f) {
    std::string finished;
    const auto results = load_last_selftest(p.fs(), &finished);
    if (!results.empty()) {
        if (!finished.empty()) f.set("selftest.last_finished", finished);
        json failed = json::array();
        int counts[7] = {};
        for (const auto& r : results) {
            f.set("test." + r.id + ".status", std::string(to_string(r.status)));
            if (!r.reason.empty()) f.set("test." + r.id + ".reason", r.reason);
            for (const auto& m : r.measurements) f.set("test." + r.id + "." + m.name, m.value);
            if (r.status == TestStatus::Failed || r.status == TestStatus::Error) failed.push_back({{"id", r.id}, {"status", std::string(to_string(r.status))}, {"reason", r.reason}});
            ++counts[static_cast<int>(r.status)];
        }
        f.set("selftest.failed", std::move(failed));
        f.set("selftest.counts.passed", counts[static_cast<int>(TestStatus::Passed)]);
        f.set("selftest.counts.skipped", counts[static_cast<int>(TestStatus::Skipped)]);
    }
    std::string marker;
    if (p.fs().read_file(kSelftestMarkerPath, marker)) {
        const json m = json::parse(marker, nullptr, false);
        if (m.is_object() && m.value("state", std::string()) == "running") {
            f.set("selftest.interrupted.started", m.value("started", std::string()));
            json tests = json::array();
            if (m.contains("tests") && m["tests"].is_array())
                for (const auto& t : m["tests"])
                    if (t.is_string()) tests.push_back(t);
            f.set("selftest.interrupted.tests", std::move(tests));
        }
    }
}

}  // namespace medic
