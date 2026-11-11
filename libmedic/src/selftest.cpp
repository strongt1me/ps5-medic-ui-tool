// SPDX-License-Identifier: GPL-3.0-or-later
#include "medic/selftest.hpp"

#include <nlohmann/json.hpp>

#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>

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

void sleep_ms(int ms) {
    if (ms <= 0) return;
    timespec ts{ms / 1000, static_cast<long>(ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

// ---- Schnelltests (nur Fakten lesen) ------------------------------------------------------------------------------------------------

using OneShotFn = TestResult (*)(const Facts&, const TestDef&);

// Der Speicher unter /data muss lesbar sein und Platz haben. Die Schwelle entspricht der Regel storage.low_space_data (5 %).
TestResult test_storage_mounts(const Facts& f, const TestDef& d) {
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
TestResult test_sensors(const Facts& f, const TestDef& d) {
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
TestResult test_db_headers(const Facts& f, const TestDef& d) {
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
TestResult test_files_sanity(const Facts& f, const TestDef& d) {
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

// Fuehrt eine Funktion in einem einzigen Schritt aus.
class OneShotSession final : public TestSession {
public:
    OneShotSession(const Facts& f, const TestDef& d, OneShotFn fn) : facts_(f), def_(d), fn_(fn) {}
    bool step() override {
        if (!done_) {
            result_ = fn_(facts_, def_);
            done_ = true;
        }
        return true;
    }
    double progress() const override { return done_ ? 1.0 : 0.0; }
    void abort(const std::string& reason) override {
        if (!done_) {
            result_ = make_result(def_, TestStatus::Cancelled, reason);
            done_ = true;
        }
    }
    TestResult result() override { return result_; }

private:
    Facts facts_;
    TestDef def_;
    OneShotFn fn_;
    bool done_ = false;
    TestResult result_;
};

// Basis fuer Sitzungen mit eigenem Ergebnis: Vorbedingung nicht erfuellt = sofort fertig (skipped).
class BaseSession : public TestSession {
public:
    explicit BaseSession(const TestDef& d) : def_(d), t0_(Clock::now()) { result_.id = d.id; }
    double progress() const override { return done_ ? 1.0 : progress_; }
    TestResult result() override { return result_; }
    void abort(const std::string& reason) override {
        if (done_) return;
        abort_reason_ = reason.empty() ? "cancelled" : reason;
        aborted_ = true;
    }

protected:
    void finish(TestStatus s, const std::string& reason) {
        result_.id = def_.id;
        result_.status = s;
        result_.reason = reason;
        done_ = true;
    }
    TestDef def_;
    Clock::time_point t0_;
    TestResult result_;
    bool done_ = false, aborted_ = false;
    std::string abort_reason_;
    double progress_ = 0;
};

// ---- Standardtest: Lesedurchsatz ----------------------------------------------------------------------------------------------------

// Liest bis zu 128 MiB der groessten vorhandenen Image-Datei (Bloecke zu 1 MiB, hoechstens 15 s, 16 MiB je Schritt) und misst den Durchsatz. Der
// Inhalt wird nicht ausgewertet. EIO oder zu wenig Daten = failed. Ohne Baseline urteilt der Test nicht ueber die Zahl, er speichert sie.
class ReadSession final : public BaseSession {
public:
    ReadSession(Platform& p, const Facts& f, const TestDef& d) : BaseSession(d), p_(p) {
        const json* images = f.get("smp.images");
        std::uint64_t best_size = 0;
        if (images && images->is_array())
            for (const auto& img : *images) {
                if (!img.is_object() || !img.contains("path") || !img["path"].is_string() || !img.contains("size") || !img["size"].is_number()) continue;
                const std::uint64_t sz = img["size"].get<std::uint64_t>();
                if (sz >= (64u << 10) && sz > best_size) {
                    best_size = sz;
                    path_ = img["path"].get<std::string>();
                }
            }
        if (path_.empty()) {
            finish(TestStatus::Skipped, "no_file");
            return;
        }
        want_ = std::min(best_size, kReadTestMaxBytes);
    }
    bool step() override {
        if (done_) return true;
        std::string reason;
        TestStatus st = TestStatus::Passed;
        bool stop = false;
        const std::uint64_t step_end = std::min<std::uint64_t>(want_, got_ + (16u << 20));
        while (!stop && got_ < step_end) {
            if (aborted_) {
                st = TestStatus::Cancelled;
                reason = abort_reason_;
                stop = true;
                break;
            }
            if (seconds_since(t0_) > kReadTestMaxSeconds) {
                reason = "time_limit";  // nicht schlimm: gemessen wird, was in der Zeit gelesen wurde
                want_ = got_;
                break;
            }
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(1u << 20, want_ - got_));
            std::string chunk;
            if (!p_.fs().read_range(path_, got_, n, chunk)) {
                st = TestStatus::Failed;
                reason = "read_error";
                stop = true;
            } else if (chunk.size() < n) {
                st = TestStatus::Failed;
                reason = "short_read";
                got_ += chunk.size();
                stop = true;
            } else {
                got_ += chunk.size();
            }
        }
        progress_ = want_ ? static_cast<double>(got_) / static_cast<double>(want_) : 1.0;
        if (aborted_ && !stop) {
            st = TestStatus::Cancelled;
            reason = abort_reason_;
            stop = true;
        }
        if (stop || got_ >= want_) {
            const double secs = std::max(seconds_since(t0_), 1e-6);
            result_.measurements.push_back({"bytes_read", static_cast<double>(got_), "B"});
            result_.measurements.push_back({"seconds", secs, "s"});
            if (st == TestStatus::Passed && got_ >= (8u << 20)) result_.measurements.push_back({"mb_per_s", static_cast<double>(got_) / 1e6 / secs, "MB/s"});  // erst ab 8 MiB aussagekraeftig
            finish(st, reason);
        }
        return done_;
    }

private:
    Platform& p_;
    std::string path_;
    std::uint64_t want_ = 0, got_ = 0;
};

// ---- Gruendlich: Waermetest (Last) --------------------------------------------------------------------------------------------------

struct Burners {
    static constexpr int kMax = 8;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> iters[kMax];
    std::time_t deadline = 0;  // harte Frist nach der Uhr: unabhaengig vom Hauptthread, die Threads beenden sich selbst
    pthread_t th[kMax];
    int started = 0;

    Burners() {
        for (auto& i : iters) i = 0;
    }
};

struct BurnArg {
    Burners* b;
    int index;
};

void* burn_main(void* p) {
    auto* a = static_cast<BurnArg*>(p);
    volatile std::uint64_t x = 1;
    for (;;) {
        for (int i = 0; i < 100000; ++i) x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        a->b->iters[a->index].fetch_add(100000, std::memory_order_relaxed);
        if (a->b->stop.load(std::memory_order_relaxed) || std::time(nullptr) >= a->b->deadline) break;
    }
    return nullptr;
}

// 5 Schritte Ruhe-Messung, dann Last auf allen Kernen (hoechstens 8), danach Abkuehlen. Abbruch bei 85 Grad oder mehr als 15 Grad Anstieg in 5
// Schritten; der Test startet nicht, wenn die Konsole in Ruhe schon ueber 70 Grad hat. Kein fork, Threads mit Frist, Join im Destruktor.
class ThermalSession final : public BaseSession {
public:
    ThermalSession(Platform& p, const TestDef& d, const TestContext& c) : BaseSession(d), p_(p), ctx_(c) {
        const int t = temp();
        if (t < 0) {
            finish(TestStatus::Skipped, "sensors_unreadable");
            return;
        }
        if (t > kThermalStartMaxC) {
            finish(TestStatus::Skipped, "too_warm");
            return;
        }
        rest_ = t;
        burn_steps_ = std::max(1, ctx_.thermal_burn_s * 1000 / std::max(1, ctx_.step_ms));
        cool_steps_ = std::max(1, ctx_.thermal_cool_s * 1000 / std::max(1, ctx_.step_ms));
        peak_ = t;
        temps_.push_back(t);
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        cores_ = static_cast<int>(std::max(1L, std::min<long>(cores, Burners::kMax)));
    }
    ~ThermalSession() override { stop_burners(); }

    bool step() override {
        if (done_) return true;
        if (aborted_) {
            stop_burners();
            conclude(TestStatus::Cancelled, abort_reason_);
            return true;
        }
        if (!burning_ && !cooling_) start_burners();
        sleep_ms(ctx_.step_ms);  // ein Messintervall
        const int t = temp();
        if (t >= 0) {
            temps_.push_back(t);
            peak_ = std::max(peak_, t);
        }
        if (burning_) {
            ++step_in_phase_;
            std::uint64_t total = 0;
            for (int i = 0; i < burners_.started; ++i) total += burners_.iters[i].load(std::memory_order_relaxed);
            rates_.push_back(total - last_total_);
            last_total_ = total;
            const std::size_t n = temps_.size();
            if (t >= kThermalCeilingC) {
                stop_burners();
                conclude(TestStatus::Cancelled, "ceiling");
                return true;
            }
            if (n > 5 && temps_[n - 1] - temps_[n - 6] > 15) {
                stop_burners();
                conclude(TestStatus::Cancelled, "rise_rate");
                return true;
            }
            if (step_in_phase_ >= burn_steps_) {
                stop_burners();
                burning_ = false;
                cooling_ = true;
                step_in_phase_ = 0;
            }
        } else if (cooling_) {
            ++step_in_phase_;
            if (step_in_phase_ >= cool_steps_) {
                conclude(TestStatus::Passed, "");
                return true;
            }
        }
        const int total_steps = burn_steps_ + cool_steps_;
        progress_ = std::min(0.99, static_cast<double>((burning_ ? 0 : burn_steps_) + step_in_phase_) / static_cast<double>(total_steps));
        return false;
    }

private:
    int temp() const {
        const Thermal t = p_.sys().thermal();
        return t.cpu_c >= 0 ? t.cpu_c : t.soc_c;
    }
    void start_burners() {
        burning_ = true;
        burners_.deadline = std::time(nullptr) + ctx_.thermal_burn_s + 3;  // harte Frist: Last plus 3 Sekunden Spielraum
        for (int i = 0; i < cores_; ++i) {
            args_[i] = {&burners_, i};
            if (pthread_create(&burners_.th[i], nullptr, burn_main, &args_[i]) != 0) break;
            ++burners_.started;
        }
        if (burners_.started == 0) {
            burning_ = false;
            conclude(TestStatus::Error, "no_threads");
        }
    }
    void stop_burners() {
        burners_.stop.store(true);
        for (int i = 0; i < burners_.started; ++i) pthread_join(burners_.th[i], nullptr);
        burners_.started = 0;
    }
    void conclude(TestStatus st, const std::string& reason) {
        const int last = temps_.empty() ? rest_ : temps_.back();
        result_.measurements.push_back({"rest_c", static_cast<double>(rest_), "C"});
        result_.measurements.push_back({"peak_c", static_cast<double>(peak_), "C"});
        result_.measurements.push_back({"rise_c", static_cast<double>(peak_ - rest_), "C"});
        result_.measurements.push_back({"end_c", static_cast<double>(last), "C"});
        result_.measurements.push_back({"cores", static_cast<double>(cores_), ""});
        std::string why = reason;
        // Durchsatzabfall der Last: Mittel der ersten und der letzten 10 Messintervalle (erst ab 20 Intervallen aussagekraeftig).
        if (rates_.size() >= 20) {
            auto mean = [&](std::size_t a, std::size_t b) {
                double s = 0;
                for (std::size_t i = a; i < b; ++i) s += static_cast<double>(rates_[i]);
                return s / static_cast<double>(b - a);
            };
            const double first = mean(0, 10), lastv = mean(rates_.size() - 10, rates_.size());
            if (first > 0) {
                const double drop = 100.0 * (first - lastv) / first;
                result_.measurements.push_back({"rate_drop_pct", drop, "%"});
                if (st == TestStatus::Passed && drop >= 10.0) {
                    st = TestStatus::Warn;
                    why = "throughput_drop";
                }
            }
        }
        if (st == TestStatus::Passed && last > rest_ + 10) {  // Abkuehlen: nach der Frist noch deutlich ueber dem Ruhewert
            st = TestStatus::Warn;
            why = "slow_cooldown";
        }
        finish(st, why);
    }

    Platform& p_;
    TestContext ctx_;
    Burners burners_;
    BurnArg args_[Burners::kMax];
    std::vector<int> temps_;
    std::vector<std::uint64_t> rates_;
    std::uint64_t last_total_ = 0;
    int rest_ = 0, peak_ = 0, cores_ = 1, burn_steps_ = 1, cool_steps_ = 1, step_in_phase_ = 0;
    bool burning_ = false, cooling_ = false;
};

// ---- Gruendlich: Schreibtest --------------------------------------------------------------------------------------------------------

// Pruefmuster: deterministischer Datenstrom je Block (xorshift64*), damit beim Zuruecklesen jede Abweichung auffaellt.
std::string pattern_block(std::uint64_t block_index, std::size_t bytes) {
    std::string out(bytes, '\0');
    std::uint64_t x = (block_index + 1) * 0x9E3779B97F4A7C15ULL;
    for (std::size_t i = 0; i + 8 <= bytes; i += 8) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        const std::uint64_t v = x * 0x2545F4914F6CDD1DULL;
        std::memcpy(&out[i], &v, 8);
    }
    return out;
}

constexpr std::size_t kWriteBlock = 4u << 20;

// Ein Opt-in-Test: ueberschreibt die EINE Testdatei (write_test.bin) im eigenen Ordner mit einem Pruefmuster und liest sie zurueck. Nie
// geloescht, nie auf USB, hoechstens ein Lauf je Tag, 256 MiB je Lauf, kumulativ hoechstens 50 GiB, Start nur mit genug freiem Platz (>= 10 % und
// Dateigroesse + 1 GiB). Abbruch laesst die Testdatei teilweise ueberschrieben (sie ist nur fuer diesen Test da).
class WriteSession final : public BaseSession {
public:
    WriteSession(Platform& p, const Facts& f, const TestDef& d, const TestContext& c) : BaseSession(d), p_(p), size_(c.write_bytes), today_(c.today) {
        if (!p.fs_rw()) {
            finish(TestStatus::Skipped, "no_write");
            return;
        }
        if (today_.size() != 8) {
            finish(TestStatus::Skipped, "no_clock");
            return;
        }
        if (size_ == 0 || size_ % kWriteBlock != 0) size_ = (size_ / kWriteBlock + 1) * kWriteBlock;
        // freier Platz auf /data
        bool found = false;
        if (const json* mounts = f.get("storage.mounts"); mounts && mounts->is_array())
            for (const auto& m : *mounts)
                if (m.is_object() && m.value("mount", std::string()) == "/data" && m.contains("free") && m["free"].is_number()) {
                    found = true;
                    const double free_bytes = m["free"].get<double>(), pct = m.value("free_pct", 0.0);
                    if (pct < 10.0 || free_bytes < static_cast<double>(size_) + static_cast<double>(1ull << 30)) {
                        finish(TestStatus::Skipped, "low_space");
                        return;
                    }
                }
        if (!found) {
            finish(TestStatus::Skipped, "no_data_mount");
            return;
        }
        // Tagesgrenze und Gesamtbudget
        std::string text;
        if (p.fs().read_file(kSelftestBudgetPath, text)) {
            const json b = json::parse(text, nullptr, false);
            if (b.is_object()) {
                if (b.value("last_date", std::string()) == today_) {
                    finish(TestStatus::Skipped, "done_today");
                    return;
                }
                budget_total_ = b.value("total_bytes", std::uint64_t{0});
                budget_runs_ = b.value("runs", 0);
                if (budget_total_ + 2 * size_ > kWriteBudgetBytes) {
                    finish(TestStatus::Skipped, "budget_used");
                    return;
                }
            }
        }
        blocks_ = size_ / kWriteBlock;
    }

    bool step() override {
        if (done_) return true;
        if (aborted_) {
            finalize(TestStatus::Cancelled, abort_reason_);
            return true;
        }
        IFileSystemRW* rw = p_.fs_rw();
        if (phase_ == 0) {  // schreiben
            const std::string data = pattern_block(index_, kWriteBlock);
            const bool last = index_ + 1 == blocks_;
            if (!rw->write_range(kSelftestWritePath, index_ * kWriteBlock, data, last)) {
                finalize(TestStatus::Failed, "write_error");
                return true;
            }
            written_ += data.size();
            if (++index_ >= blocks_) {
                phase_ = 1;
                write_secs_ = seconds_since(t0_);
                index_ = 0;
            }
        } else {  // zuruecklesen und vergleichen
            std::string got;
            if (!p_.fs().read_range(kSelftestWritePath, index_ * kWriteBlock, kWriteBlock, got)) {
                finalize(TestStatus::Failed, "read_error");
                return true;
            }
            if (got != pattern_block(index_, kWriteBlock)) {
                ++bad_blocks_;
                if (first_bad_ < 0) first_bad_ = static_cast<double>(index_ * kWriteBlock);
            }
            if (++index_ >= blocks_) {
                finalize(bad_blocks_ ? TestStatus::Failed : TestStatus::Passed, bad_blocks_ ? "verify_mismatch" : "");
                return true;
            }
        }
        progress_ = static_cast<double>((phase_ == 0 ? 0 : blocks_) + index_) / static_cast<double>(2 * blocks_);
        return false;
    }

private:
    void finalize(TestStatus st, const std::string& reason) {
        const double secs = std::max(seconds_since(t0_), 1e-6);
        result_.measurements.push_back({"bytes_written", static_cast<double>(written_), "B"});
        result_.measurements.push_back({"bad_blocks", static_cast<double>(bad_blocks_), ""});
        if (first_bad_ >= 0) result_.measurements.push_back({"first_bad_offset", first_bad_, "B"});
        if (write_secs_ > 0 && written_ >= (64u << 20)) result_.measurements.push_back({"write_mb_per_s", static_cast<double>(written_) / 1e6 / write_secs_, "MB/s"});
        result_.measurements.push_back({"seconds", secs, "s"});
        // Budget fortschreiben, sobald wirklich geschrieben wurde (auch bei Abbruch oder Fehler: so wiederholt kein Lauf ungebremst)
        if (written_ > 0)
            p_.fs_rw()->write_file(kSelftestBudgetPath, json({{"last_date", today_}, {"runs", budget_runs_ + 1}, {"total_bytes", budget_total_ + written_}}).dump() + "\n");
        finish(st, reason);
    }

    Platform& p_;
    std::uint64_t size_, blocks_ = 0, index_ = 0, written_ = 0, budget_total_ = 0, bad_blocks_ = 0;
    int budget_runs_ = 0, phase_ = 0;
    double write_secs_ = 0, first_bad_ = -1;
    std::string today_;
};

struct Entry {
    TestDef def;
    OneShotFn fn;  // nur Schnelltests; sonst nullptr
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> list = {
        {{"sw.storage.mounts", TestTier::Quick, "none", 1, "Speicher unter /data", "Storage under /data", "Zustand der Hardware, Verschleiß", "hardware condition, wear"}, test_storage_mounts},
        {{"hw.sensors.plausible", TestTier::Quick, "none", 1, "Temperatursensoren", "Temperature sensors", "Kühlleistung, Lüfter, Verhalten unter Last", "cooling performance, fan, behaviour under load"}, test_sensors},
        {{"sw.db.headers", TestTier::Quick, "none", 2, "Datenbanken der Systemsoftware (Kopf und Größe)", "System software databases (header and size)", "Inhalt der Datenbanken (siehe Datenbank-Prüfung)", "database content (see the database check)"}, test_db_headers},
        {{"sw.files.sanity", TestTier::Quick, "none", 2, "Payloads, Images und eboot.bin (Kopf)", "Payloads, images and eboot.bin (header)", "Inhalt der Dateien, Entschlüsseln, Start des Spiels", "file contents, decryption, whether a game starts"}, test_files_sanity},
        {{"hw.storage.read_throughput", TestTier::Standard, "read", 20, "Lesetest einer Image-Datei", "Read test of an image file", "Schreibleistung, Verschleiß, ungelesene Bereiche", "write speed, wear, unread areas"}, nullptr},
        {{"hw.thermal.short_soak", TestTier::Thorough, "load", 80, "Wärmetest (kurze CPU-Last)", "Heat test (short CPU load)", "GPU-/Speicherwärme, Lüfter, Ursache (Staub, Wärmeleitmaterial); bei kühler Umgebung können Probleme verborgen bleiben", "GPU/memory heat, fan, cause (dust, thermal paste); in a cool room problems can stay hidden"}, nullptr},
        {{"hw.storage.write_verify", TestTier::Thorough, "write", 30, "Schreibtest (eine Testdatei, 256 MiB)", "Write test (one test file, 256 MiB)", "Restlebensdauer, Verschleiß, Leistung außerhalb des Schreib-Caches; der Rücklesevergleich kann aus dem Zwischenspeicher kommen", "remaining lifetime, wear, speed beyond the write cache; the read-back may come from cache"}, nullptr},
    };
    return list;
}

json result_json(const TestResult& r) {
    json m = json::array();
    for (const auto& x : r.measurements) m.push_back({{"name", x.name}, {"value", x.value}, {"unit", x.unit}});
    return {{"id", r.id}, {"status", to_string(r.status)}, {"reason", r.reason}, {"started", r.started}, {"duration_s", r.duration_s}, {"measurements", m}};
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

std::unique_ptr<TestSession> begin_selftest_session(Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx) {
    for (const auto& e : entries()) {
        if (e.def.id != def.id) continue;
        if (e.fn) return std::make_unique<OneShotSession>(facts, e.def, e.fn);
        if (e.def.id == "hw.storage.read_throughput") return std::make_unique<ReadSession>(p, facts, e.def);
        if (e.def.id == "hw.thermal.short_soak") return std::make_unique<ThermalSession>(p, e.def, ctx);
        if (e.def.id == "hw.storage.write_verify") return std::make_unique<WriteSession>(p, facts, e.def, ctx);
    }
    return std::make_unique<OneShotSession>(facts, def, [](const Facts&, const TestDef& d) { return make_result(d, TestStatus::Error, "unknown_test"); });
}

TestResult run_selftest(Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx) {
    const auto t0 = Clock::now();
    TestResult r;
    try {
        auto s = begin_selftest_session(p, facts, def, ctx);
        for (;;) {
            if (ctx.cancelled && ctx.cancelled()) s->abort("cancelled");  // auch vor dem ersten Schritt
            if (s->step()) break;
        }
        r = s->result();
    } catch (const std::exception&) {
        r = make_result(def, TestStatus::Error, "exception");  // ein Fehler im Test ist nie "passed"
    }
    r.id = def.id;
    r.duration_s = seconds_since(t0);
    return r;
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
