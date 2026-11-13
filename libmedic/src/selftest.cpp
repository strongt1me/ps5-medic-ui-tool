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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <thread>

#if defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace medic {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// Sekunden einer monotonen Uhr: die harte Frist der Rechenthreads darf nicht von der Wanduhr abhaengen (Netzzeit, manuelle Aenderung)
std::int64_t steady_seconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

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
            // ein exFAT-Image, dessen Kopf nicht lesbar oder ungueltig war (keine Volumengroesse), gilt nicht als geprueft
            const bool exfat_head_missing = img.value("ext", std::string()) == ".exfat" && !img.contains("volume_bytes");
            if (img.value("truncated", 0) == 1 || img.value("tiny", 0) == 1 || exfat_head_missing) ++bad_images;
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
        const auto step_t0 = Clock::now();
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
        active_ += seconds_since(step_t0);
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
            // Durchsatz aus der reinen Lesezeit: die Schritte laufen im Takt der Oberflaeche (alle 33 ms), die Pausen dazwischen gehoeren nicht dazu
            // (sonst erschiene jedes schnelle Laufwerk als hoechstens ~0,5 GB/s)
            if (st == TestStatus::Passed && got_ >= (8u << 20)) result_.measurements.push_back({"mb_per_s", static_cast<double>(got_) / 1e6 / std::max(active_, 1e-6), "MB/s"});  // erst ab 8 MiB aussagekraeftig
            if (st == TestStatus::Passed && reason == "time_limit" && got_ < (8u << 20)) {  // in der Zeit kaum etwas gelesen: kein glattes "bestanden"
                st = TestStatus::Warn;
                reason = "slow_read";
            }
            finish(st, reason);
        }
        return done_;
    }

private:
    Platform& p_;
    std::string path_;
    std::uint64_t want_ = 0, got_ = 0;
    double active_ = 0;  // Sekunden, in denen wirklich gelesen wurde
};

// ---- Gruendlich: Waermetest (Last) --------------------------------------------------------------------------------------------------

struct Burners {
    static constexpr int kMax = 8;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> errors{0};  // Runden, deren Ergebnis von der Referenz abwich (stiller Rechenfehler)
    std::uint32_t reference = 0;
    bool use_fma = false, inject = false;
    std::atomic<std::uint64_t> iters[kMax];
    std::int64_t deadline = 0;  // harte Frist nach der Uhr: unabhaengig vom Hauptthread, die Threads beenden sich selbst
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

#if defined(__x86_64__)
// Kraeftige Last: Fused-Multiply-Add auf 8 unabhaengigen 256-Bit-Registern (heizt deutlich mehr als eine Ganzzahlschleife). Nur wenn die CPU
// AVX2 und FMA meldet; sonst die Ganzzahlschleife.
__attribute__((target("avx2,fma"))) float fma_kernel(int rounds) {
    __m256 a0 = _mm256_set1_ps(1.0f), a1 = _mm256_set1_ps(1.1f), a2 = _mm256_set1_ps(1.2f), a3 = _mm256_set1_ps(1.3f);
    __m256 a4 = _mm256_set1_ps(1.4f), a5 = _mm256_set1_ps(1.5f), a6 = _mm256_set1_ps(1.6f), a7 = _mm256_set1_ps(1.7f);
    const __m256 m = _mm256_set1_ps(0.999999f), c = _mm256_set1_ps(0.000001f);
    for (int i = 0; i < rounds; ++i) {
        a0 = _mm256_fmadd_ps(a0, m, c);
        a1 = _mm256_fmadd_ps(a1, m, c);
        a2 = _mm256_fmadd_ps(a2, m, c);
        a3 = _mm256_fmadd_ps(a3, m, c);
        a4 = _mm256_fmadd_ps(a4, m, c);
        a5 = _mm256_fmadd_ps(a5, m, c);
        a6 = _mm256_fmadd_ps(a6, m, c);
        a7 = _mm256_fmadd_ps(a7, m, c);
    }
    __m256 sum = _mm256_add_ps(_mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3)), _mm256_add_ps(_mm256_add_ps(a4, a5), _mm256_add_ps(a6, a7)));
    float out[8];
    _mm256_storeu_ps(out, sum);
    return out[0] + out[7];
}
// AVX2 und FMA per cpuid melden und das Betriebssystem muss die 256-Bit-Register sichern (xgetbv); ohne __builtin_cpu_supports, das auf der PS5-Toolchain
// __cpu_model aus compiler-rt braucht.
bool have_fma() {
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
    const bool osxsave = ecx & (1u << 27), avx = ecx & (1u << 28), fma = ecx & (1u << 12);
    if (!osxsave || !avx || !fma) return false;
    unsigned xlo = 0, xhi = 0;
    __asm__ volatile("xgetbv" : "=a"(xlo), "=d"(xhi) : "c"(0));
    if ((xlo & 6u) != 6u) return false;
    if (__get_cpuid_max(0, nullptr) < 7) return false;
    __cpuid_count(7, 0, eax, ebx, ecx, edx);
    return ebx & (1u << 5);  // AVX2
}
#else
float fma_kernel(int) { return 0; }
bool have_fma() { return false; }
#endif

// Eine Runde der Last mit festem Ergebnis (Bitmuster): Auf einer gesunden CPU ist es bei jeder Wiederholung und auf jedem Kern gleich.
float int_kernel() {
    volatile std::uint64_t x = 1;
    for (int i = 0; i < 100000; ++i) x = x * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<float>(static_cast<std::uint32_t>(x >> 40));
}

std::uint32_t round_bits(bool fma) {
    const float r = fma ? fma_kernel(20000) : int_kernel();
    std::uint32_t b;
    std::memcpy(&b, &r, sizeof b);
    return b;
}

void* burn_main(void* p) {
    auto* a = static_cast<BurnArg*>(p);
    std::uint64_t round = 0;
    for (;;) {
        std::uint32_t bits = round_bits(a->b->use_fma);
        if (a->b->inject && a->index == 0 && round == 2) bits ^= 0x400;  // Testhilfe: ein gekipptes Bit
        if (bits != a->b->reference) a->b->errors.fetch_add(1, std::memory_order_relaxed);
        ++round;
        a->b->iters[a->index].fetch_add(1, std::memory_order_relaxed);  // eine Runde = ein Zaehler (Einheit egal: nur das Verhaeltnis zaehlt)
        if (a->b->stop.load(std::memory_order_relaxed) || steady_seconds() >= a->b->deadline) break;
    }
    return nullptr;
}

// Ruhe-Messung, dann Last auf allen Kernen (hoechstens 8), danach Abkuehlen. Die Phasen richten sich nach der ECHTEN Zeit (steady_clock), nicht
// nach der Schrittzahl: ein Schritt kann auf der Konsole laenger dauern als angefordert (am 11. Nov. 2026 bis zu 1,4 s statt 1 s). Abbruch bei 85
// Grad oder mehr als 15 Grad Anstieg in 5 Messungen; der Test startet nicht, wenn die Konsole in Ruhe schon ueber 70 Grad hat. Kein fork, Threads mit
// Frist, Join im Destruktor.
class ThermalSession final : public BaseSession {
public:
    ThermalSession(Platform& p, const Facts& facts, const TestDef& d, const TestContext& c) : BaseSession(d), p_(p), ctx_(c) {
        if (const json* n = facts.get("fan.controller.name"); n && n->is_string()) fan_controller_ = true;
        if (const json* t = facts.get("fan.controller.target_c"); t && t->is_number()) fan_target_ = t->get<double>();
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
        peak_ = t;
        temps_.push_back(t);
        long cores = sysconf(_SC_NPROCESSORS_ONLN);
        // Zwei Kerne bleiben frei (Dienst, App, ShadowMount+): bei voller Last auf allen Kernen kam der Dienst zeitweise nicht mehr zum Lesen seiner Eingaben,
        // die App brach die Verbindung ab und die Oberflaeche startete mitten im Test neu (Konsole, 13. Nov. 2026)
        cores_ = static_cast<int>(std::max(1L, std::min<long>(cores > 2 ? cores - 2 : cores, Burners::kMax)));
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
        if (done_) return true;
        // Ein Messintervall dauert step_ms, aber step() schlaeft nur kurz (hoechstens 20 ms) und kehrt dann zurueck: der Dienst ruft es aus seiner
        // Hauptschleife, die zwischendurch Eingaben lesen und Bilder senden muss (frueher schlief sie eine ganze Sekunde je Aufruf)
        if (Clock::now() < next_sample_) {
            const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(next_sample_ - Clock::now()).count();
            sleep_ms(static_cast<int>(std::min<long long>(std::max<long long>(wait, 0), 20)));
            if (Clock::now() < next_sample_) return false;
        }
        next_sample_ = Clock::now() + std::chrono::milliseconds(ctx_.step_ms);
        const double now = seconds_since(phase_start_);
        const int t = temp();
        if (t >= 0) {
            temps_.push_back(t);
            peak_ = std::max(peak_, t);
            sensor_miss_ = 0;
        } else if (burning_ && ++sensor_miss_ >= 3) {  // ohne Messwert gibt es keinen Schutz vor Ueberhitzung: unter Last nicht weiterlaufen
            stop_burners();
            conclude(TestStatus::Cancelled, "sensor_lost");
            return true;
        }
        if (burning_) {
            std::uint64_t total = 0;
            for (int i = 0; i < burners_.started; ++i) total += burners_.iters[i].load(std::memory_order_relaxed);
            const double dt = std::max(now - last_sample_, 1e-3);
            rates_.push_back(static_cast<double>(total - last_total_) / dt);  // Runden je Sekunde
            last_total_ = total;
            last_sample_ = now;
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
            progress_ = std::min(0.99, 0.6 * now / std::max(1, ctx_.thermal_burn_s));
            if (now >= ctx_.thermal_burn_s) {
                stop_burners();
                burning_ = false;
                cooling_ = true;
                phase_start_ = Clock::now();
                next_sample_ = phase_start_ + std::chrono::milliseconds(ctx_.step_ms);
            }
        } else if (cooling_) {
            progress_ = std::min(0.99, 0.6 + 0.4 * now / std::max(1, ctx_.thermal_cool_s));
            if (now >= ctx_.thermal_cool_s) {
                conclude(TestStatus::Passed, "");
                return true;
            }
        }
        return false;
    }

private:
    int temp() const {
        const Thermal t = p_.sys().thermal();
        return t.cpu_c >= 0 ? t.cpu_c : t.soc_c;
    }
    void start_burners() {
        burning_ = true;
        phase_start_ = Clock::now();
        next_sample_ = phase_start_ + std::chrono::milliseconds(ctx_.step_ms);
        last_sample_ = 0;
        burners_.deadline = steady_seconds() + ctx_.thermal_burn_s + 3;  // harte Frist: Last plus 3 Sekunden Spielraum
        burners_.use_fma = have_fma();
        burners_.reference = round_bits(burners_.use_fma);  // Referenz im Hauptthread, vor den Rechenthreads
        burners_.inject = ctx_.inject_compute_fault;
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
        const std::uint64_t compute_errors = burners_.errors.load();
        result_.measurements.push_back({"compute_errors", static_cast<double>(compute_errors), ""});
        if (compute_errors > 0) {  // ein stiller Rechenfehler unter Last ist ein harter Befund, auch bei Abbruch
            st = TestStatus::Failed;
            why = "compute_mismatch";
        }
        // Durchsatzabfall der Last: Mittel der ersten und der letzten 10 Messintervalle (erst ab 20 Intervallen aussagekraeftig).
        if (rates_.size() >= 20 && ctx_.thermal_burn_s >= 20) {  // kurze Laeufe (Tests) urteilen nicht ueber die Rate
            auto mean = [&](std::size_t a, std::size_t b) {
                double s = 0;
                for (std::size_t i = a; i < b; ++i) s += rates_[i];
                return s / static_cast<double>(b - a);
            };
            const double first = mean(0, 10), lastv = mean(rates_.size() - 10, rates_.size());
            result_.measurements.push_back({"rounds_per_s_first", first, ""});
            result_.measurements.push_back({"rounds_per_s_last", lastv, ""});
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
        // Bestanden, aber kaum erwaermt: die Aussage ist schwach (Last zu schwach oder sehr gute Kuehlung), das steht im Ergebnis.
        // Ein Luefterregler haelt die Temperatur niedrig: das wird genannt, und die Zieltemperatur steht im Ergebnis.
        if (fan_controller_ && fan_target_ >= 0) result_.measurements.push_back({"fan_controller_target_c", fan_target_, "C"});
        if (st == TestStatus::Passed && peak_ - rest_ < 5) why = fan_controller_ ? "fan_controller" : "no_heating";
        finish(st, why);
    }

    Platform& p_;
    TestContext ctx_;
    Burners burners_;
    BurnArg args_[Burners::kMax];
    std::vector<int> temps_;
    int sensor_miss_ = 0;  // aufeinanderfolgende Intervalle ohne Temperaturwert
    std::vector<double> rates_;
    std::uint64_t last_total_ = 0;
    double last_sample_ = 0;
    Clock::time_point phase_start_ = Clock::now();
    Clock::time_point next_sample_ = Clock::now();
    int rest_ = 0, peak_ = 0, cores_ = 1;
    bool burning_ = false, cooling_ = false;
    bool fan_controller_ = false;
    double fan_target_ = -1;
};

// ---- Gruendlich: Speicher-Muster-Test (Userland) ------------------------------------------------------------------------------------

// Belegt bis zu 128 MiB (bei Allokationsfehler halbiert, mindestens 16 MiB), beschreibt sie nacheinander mit sechs Mustern (0x00, 0xFF, 0x55,
// 0xAA, laufende Eins, Zufall) und liest jedes Muster zurueck. Das prueft nur den Teil des Speichers, den ein Prozess bekommt: nicht den
// gesamten GDDR6, keinen GPU-Zugriff, keine ECC. Jedes abweichende Wort ist ein Fehler; Wiederholung an derselben Adresse steht im Bericht.
class MemorySession final : public BaseSession {
public:
    MemorySession(const TestDef& d, const TestContext& c) : BaseSession(d), inject_(c.inject_mem_fault) {
        std::uint64_t size = std::max<std::uint64_t>(c.mem_bytes, kBlock);
        while (size >= kBlock) {
            buf_ = static_cast<std::uint64_t*>(std::malloc(static_cast<std::size_t>(size)));
            if (buf_) break;
            size /= 2;
        }
        if (!buf_ || size < kBlock) {
            finish(TestStatus::Skipped, "no_memory");
            return;
        }
        size_ = size - size % kBlock;
        blocks_ = size_ / kBlock;
    }
    ~MemorySession() override { std::free(buf_); }

    bool step() override {
        if (done_) return true;
        if (aborted_) {
            conclude(TestStatus::Cancelled, abort_reason_);
            return true;
        }
        std::uint64_t* p = buf_ + block_ * (kBlock / 8);
        const std::size_t words = kBlock / 8;
        if (phase_ == 0) {  // Muster schreiben
            fill(p, words, pattern_, block_);
            if (inject_ && pattern_ == 1 && block_ == 0) p[3] ^= 0x10;  // Testhilfe: ein gekipptes Bit
        } else {  // zuruecklesen
            std::vector<std::uint64_t> expect(words);
            fill(expect.data(), words, pattern_, block_);
            for (std::size_t i = 0; i < words; ++i)
                if (p[i] != expect[i]) {
                    ++bad_words_;
                    if (first_bad_ < 0) first_bad_ = static_cast<double>(block_ * kBlock + i * 8);
                }
        }
        if (++block_ >= blocks_) {
            block_ = 0;
            if (phase_ == 0) {
                phase_ = 1;
            } else {
                phase_ = 0;
                if (++pattern_ >= kPatterns) {
                    conclude(bad_words_ ? TestStatus::Failed : TestStatus::Passed, bad_words_ ? "mem_mismatch" : "");
                    return true;
                }
            }
        }
        progress_ = std::min(0.99, (static_cast<double>(pattern_) * 2 * blocks_ + (phase_ ? blocks_ : 0) + block_) / (static_cast<double>(kPatterns) * 2 * blocks_));
        return false;
    }

private:
    static constexpr std::size_t kBlock = 16u << 20;
    static constexpr int kPatterns = 6;
    static void fill(std::uint64_t* p, std::size_t words, int pattern, std::uint64_t block) {
        std::uint64_t x = (block + 1) * 0x9E3779B97F4A7C15ULL;
        for (std::size_t i = 0; i < words; ++i) {
            switch (pattern) {
                case 0: p[i] = 0x0000000000000000ULL; break;
                case 1: p[i] = 0xFFFFFFFFFFFFFFFFULL; break;
                case 2: p[i] = 0x5555555555555555ULL; break;
                case 3: p[i] = 0xAAAAAAAAAAAAAAAAULL; break;
                case 4: p[i] = 1ULL << ((i + block) % 64); break;  // laufende Eins
                default:
                    x ^= x >> 12;
                    x ^= x << 25;
                    x ^= x >> 27;
                    p[i] = x * 0x2545F4914F6CDD1DULL;
            }
        }
    }
    void conclude(TestStatus st, const std::string& reason) {
        result_.measurements.push_back({"bytes_tested", static_cast<double>(size_), "B"});
        result_.measurements.push_back({"patterns", static_cast<double>(std::min(pattern_ + (phase_ ? 1 : 0), kPatterns)), ""});
        result_.measurements.push_back({"bad_words", static_cast<double>(bad_words_), ""});
        if (first_bad_ >= 0) result_.measurements.push_back({"first_bad_offset", first_bad_, "B"});
        result_.measurements.push_back({"seconds", std::max(seconds_since(t0_), 1e-6), "s"});
        finish(st, reason);
    }

    std::uint64_t* buf_ = nullptr;
    std::uint64_t size_ = 0, blocks_ = 0, block_ = 0, bad_words_ = 0;
    int phase_ = 0, pattern_ = 0;
    double first_bad_ = -1;
    bool inject_;
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
            if (!b.is_object()) {  // ungueltig: nicht stillschweigend ohne Tages- und Gesamtgrenze weiterschreiben
                finish(TestStatus::Skipped, "budget_unreadable");
                return;
            }
            const std::string last = b.contains("last_date") && b["last_date"].is_string() ? b["last_date"].get<std::string>() : std::string();
            if (last == today_) {
                finish(TestStatus::Skipped, "done_today");
                return;
            }
            budget_total_ = b.contains("total_bytes") && b["total_bytes"].is_number_unsigned() ? b["total_bytes"].get<std::uint64_t>() : 0;
            budget_runs_ = b.contains("runs") && b["runs"].is_number_integer() ? b["runs"].get<int>() : 0;
            prior_date_ = last;
            if (budget_total_ + 2 * size_ > kWriteBudgetBytes) {
                finish(TestStatus::Skipped, "budget_used");
                return;
            }
        } else if (p.fs().exists(kSelftestBudgetPath)) {  // vorhanden, aber nicht lesbar
            finish(TestStatus::Skipped, "budget_unreadable");
            return;
        }
        // Tagesmarke VOR dem ersten Schreiben setzen: laesst sich das Budget nicht schreiben, laeuft der Test gar nicht (sonst waere die
        // Tagesgrenze wirkungslos), und bricht die Konsole mitten im Test ab, laeuft er am selben Tag nicht noch einmal.
        if (!write_budget(today_, budget_runs_, budget_total_)) {
            finish(TestStatus::Skipped, "budget_unwritable");
            return;
        }
        blocks_ = size_ / kWriteBlock;
    }

    bool step() override {
        if (done_) return true;
        if (aborted_) {
            finalize(TestStatus::Cancelled, abort_reason_);
            return true;
        }
        if (seconds_since(t0_) > kWriteTestMaxSeconds) {  // haengendes Laufwerk: nicht ohne Grenze weiterschreiben und lesen
            finalize(TestStatus::Cancelled, "time_limit");
            return true;
        }
        struct Active {  // zaehlt nur die Zeit in step(), nicht die Pausen zwischen den Schritten (Takt der Oberflaeche)
            WriteSession* s;
            ~Active() { s->active_ += seconds_since(s->step_t0_); }
        } active{this};
        step_t0_ = Clock::now();
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
                write_secs_ = active_ + seconds_since(step_t0_);
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
        // Ist nichts geschrieben worden (sofort abgebrochen), wird die Tagesmarke vom Anfang wieder zurueckgenommen.
        if (written_ > 0) write_budget(today_, budget_runs_ + 1, budget_total_ + written_);
        else write_budget(prior_date_, budget_runs_, budget_total_);
        finish(st, reason);
    }

    bool write_budget(const std::string& date, int runs, std::uint64_t total) {
        return p_.fs_rw()->write_file(kSelftestBudgetPath, json({{"last_date", date}, {"runs", runs}, {"total_bytes", total}}).dump() + "\n");
    }

    Platform& p_;
    std::uint64_t size_, blocks_ = 0, index_ = 0, written_ = 0, budget_total_ = 0, bad_blocks_ = 0;
    int budget_runs_ = 0, phase_ = 0;
    double write_secs_ = 0, first_bad_ = -1;
    double active_ = 0;
    Clock::time_point step_t0_ = Clock::now();
    std::string today_, prior_date_;
};

// ---- Standardtest: lokales Netz (nur das Gateway) -------------------------------------------------------------------------------------

// Nur Adressen im privaten Bereich (RFC 1918 und Link-Local): Der Test spricht ausschliesslich das Gateway im eigenen Netz an, nie das Internet.
bool is_private_ipv4(const std::string& ip) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(ip.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return false;
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
}

// Misst Antwortzeit und Verlust zum Standard-Gateway mit TCP-Verbindungsversuchen (nur der Verbindungsaufbau, keine Daten). Ein Port ohne Dienst
// antwortet mit RST und zaehlt als Antwort. Antwortet das Gateway auf keinen der Ports (viele Router verwerfen still), bleibt der Test skipped:
// Schweigen ist keine Aussage ueber das Netz. Nie failed. Warnung nur bei deutlichem Verlust oder deutlich langer Antwortzeit.
class NetLocalSession final : public BaseSession {
public:
    NetLocalSession(Platform& p, const Facts& f, const TestDef& d, const TestContext& ctx) : BaseSession(d), p_(p), gap_ms_(ctx.net_gap_ms) {
        const json* gw = f.get("net.gateway");
        if (!gw || !gw->is_string() || gw->get<std::string>().empty()) {
            finish(TestStatus::Skipped, "no_gateway");
            return;
        }
        gateway_ = gw->get<std::string>();
        if (!is_private_ipv4(gateway_)) finish(TestStatus::Skipped, "not_local");  // Sicherung: nie etwas ausserhalb des eigenen Netzes ansprechen
    }
    bool step() override {
        if (done_) return true;
        if (aborted_) {
            finish(TestStatus::Cancelled, abort_reason_);
            return true;
        }
        if (port_ == 0) {  // Port suchen, auf dem das Gateway antwortet
            bool any_error = false;
            for (int port : {80, 443, 53, 22}) {
                const auto o = p_.sys().tcp_probe(gateway_, port, kProbeTimeoutMs);
                if (o.result == ISystemInfo::ProbeResult::Connected || o.result == ISystemInfo::ProbeResult::Refused) {
                    port_ = port;
                    replies_++;
                    rtts_.push_back(o.rtt_ms);
                    ++sent_;
                    break;
                }
                any_error = any_error || o.result == ISystemInfo::ProbeResult::Error;
            }
            if (port_ == 0) {
                finish(TestStatus::Skipped, any_error ? "probe_error" : "no_response");
                return true;
            }
            progress_ = 0.1;
            return false;
        }
        for (int i = 0; i < 5 && sent_ < kProbes; ++i) {
            if (gap_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(gap_ms_));
            const auto o = p_.sys().tcp_probe(gateway_, port_, kProbeTimeoutMs);
            ++sent_;
            if (o.result == ISystemInfo::ProbeResult::Connected || o.result == ISystemInfo::ProbeResult::Refused) {
                replies_++;
                rtts_.push_back(o.rtt_ms);
            } else if (o.result == ISystemInfo::ProbeResult::Timeout) {
                lost_++;
            } else {
                --sent_;  // Fehler beim Senden zaehlt weder als Antwort noch als Verlust
                if (++errors_ >= 3) {
                    finish(TestStatus::Skipped, "probe_error");
                    return true;
                }
            }
        }
        progress_ = 0.1 + 0.9 * static_cast<double>(sent_) / kProbes;
        if (sent_ < kProbes) return false;
        const int counted = replies_ + lost_;
        const double loss = counted > 0 ? 100.0 * lost_ / counted : 0.0;
        double sum = 0, mx = 0;
        for (double r : rtts_) {
            sum += r;
            mx = std::max(mx, r);
        }
        const double avg = rtts_.empty() ? 0.0 : sum / static_cast<double>(rtts_.size());
        result_.measurements.push_back({"probes", static_cast<double>(counted), ""});
        result_.measurements.push_back({"loss_pct", loss, "%"});
        result_.measurements.push_back({"rtt_avg_ms", avg, "ms"});
        result_.measurements.push_back({"rtt_max_ms", mx, "ms"});
        if (loss >= kLossWarnPct) finish(TestStatus::Warn, "packet_loss");
        else if (avg > kRttWarnMs) finish(TestStatus::Warn, "slow_gateway");
        else finish(TestStatus::Passed, "");
        return true;
    }

private:
    static constexpr int kProbes = 20, kProbeTimeoutMs = 1000;
    static constexpr double kLossWarnPct = 20.0, kRttWarnMs = 100.0;
    Platform& p_;
    int gap_ms_;
    std::string gateway_;
    int port_ = 0, sent_ = 0, replies_ = 0, lost_ = 0, errors_ = 0;
    std::vector<double> rtts_;
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
        {{"hw.storage.read_throughput", TestTier::Standard, "read", 20, "Lesetest einer Image-Datei", "Read test of an image file", "Schreibleistung, Verschleiß, ungelesene Bereiche; wiederholte Läufe können aus dem Zwischenspeicher lesen", "write speed, wear, unread areas; repeated runs may read from the cache"}, nullptr},
        {{"sw.net.local", TestTier::Standard, "net_local", 12, "Lokales Netz (Antwortzeit zum Router)", "Local network (response time to the router)", "Internet, Download-Geschwindigkeit, WLAN-Signalstärke, andere Geräte im Netz; nur die Verbindung zwischen Konsole und Router, ohne Daten zu senden", "internet, download speed, Wi-Fi signal strength, other devices; only the link between console and router, without sending data"}, nullptr},
        {{"hw.thermal.short_soak", TestTier::Thorough, "load", 35, "Wärmetest (kurze CPU-Last)", "Heat test (short CPU load)", "GPU-/Speicherwärme, die Standardkühlung bei laufendem Lüfterregler (er hält die Konsole kühl), Ursache (Staub, Wärmeleitmaterial); in kühler Umgebung bleiben Probleme verborgen", "GPU/memory heat, the standard cooling while a fan controller runs (it keeps the console cool), cause (dust, thermal paste); in a cool room problems stay hidden"}, nullptr},
        {{"hw.mem.userland_pattern", TestTier::Thorough, "load", 20, "Speicher-Muster-Test (128 MiB Arbeitsspeicher)", "Memory pattern test (128 MiB of working memory)", "Gesamter GDDR6, GPU-Zugriff, ECC; nur der Speicher, den ein Prozess bekommt", "all GDDR6, GPU access, ECC; only the memory a process gets"}, nullptr},
        {{"hw.storage.write_verify", TestTier::Thorough, "write", 30, "Schreibtest (eine Testdatei, 256 MiB)", "Write test (one test file, 256 MiB)", "Restlebensdauer, Verschleiß, Leistung außerhalb des Schreib-Caches; der Rücklesevergleich kann aus dem Zwischenspeicher kommen", "remaining lifetime, wear, speed beyond the write cache; the read-back may come from cache"}, nullptr},
        {{"hw.pad.buttons", TestTier::User, "none", 60, "Controller-Tasten (Nutzer-Test)", "Controller buttons (user test)", "Sticks, Trigger, Drift, Funkqualität, Akkukapazität; nur ob jede Taste ankommt", "sticks, triggers, drift, radio quality, battery capacity; only whether every button arrives"}, nullptr},
        {{"hw.display.pattern", TestTier::User, "none", 60, "Anzeige-Muster (Nutzer-Test)", "Display patterns (user test)", "Panel, HDMI-Anschluss und Kabel im Detail, Bildschärfe; die Beurteilung macht der Mensch (Standard: „weiß nicht“)", "panel, HDMI port and cable in detail, sharpness; the judgement is the human's (default: “don't know”)"}, nullptr},
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

// to_string(TestStatus) steht in finding.cpp (ohne Ausnahmen, auch in der nativen App)

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
        if (e.def.tier == TestTier::User)
            return std::make_unique<OneShotSession>(facts, e.def, [](const Facts&, const TestDef& d) { return make_result(d, TestStatus::Skipped, "needs_user"); });
        if (e.def.id == "sw.net.local") return std::make_unique<NetLocalSession>(p, facts, e.def, ctx);
        if (e.def.id == "hw.mem.userland_pattern") return std::make_unique<MemorySession>(e.def, ctx);
        if (e.def.id == "hw.thermal.short_soak") return std::make_unique<ThermalSession>(p, facts, e.def, ctx);
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

bool selftest_record(Platform& p, const std::string& ts, const TestResult& r) {
    IFileSystemRW* rw = p.fs_rw();
    if (!rw || !valid_ts(ts)) return false;
    std::string finished = ts;
    std::vector<TestResult> all = load_last_selftest(p.fs(), &finished);
    if (finished.empty()) finished = ts;
    bool replaced = false;
    for (auto& x : all)
        if (x.id == r.id) {
            x = r;
            replaced = true;
        }
    if (!replaced) all.push_back(r);
    json arr = json::array();
    for (const auto& x : all) arr.push_back(result_json(x));
    return rw->write_file(kSelftestLastPath, json({{"finished", finished}, {"cancelled", false}, {"results", arr}}).dump(2) + "\n");
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
