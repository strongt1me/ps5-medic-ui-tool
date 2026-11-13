// SPDX-License-Identifier: GPL-3.0-or-later
// Selbsttest-Framework (Batch 7 und 8, docs/DIAGNOSE.md). Ein Test ist ein aktiver Collector: Er liefert ein Ergebnis mit Status und Messwerten,
// das als Fakten `test.<id>.*` in dieselbe Regel-Engine fliesst. Risiko je Test: none (nur Fakten), read (liest Dateien), load (CPU-Last),
// write (schreibt EINE Testdatei im eigenen Ordner). Geschrieben wird ausserhalb des Schreibtests nur unter /data/ps5medic/selftest/ (Marker,
// letztes Ergebnis, Budget; immer ueberschrieben, nie geloescht).
//
// Stufen: Schnell (nur Fakten), Standard (liest), Gruendlich (Last, Speicher belegen und Schreiben; nur nach ausdruecklicher Bestaetigung).
//
// Statusmodell: passed (nicht aufgefallen, nie "gesund"), warn, failed, error (Test selbst gescheitert), cancelled (Abbruch, mit Grund),
// skipped (Vorbedingung fehlt, nie passed), not_run (noch nicht gelaufen). Faellt eine Vorbedingung aus, ist der Test skipped, nie failed.
// Ohne Baseline urteilen Messwert-Tests nicht ueber Zahlen: sie speichern Rohwerte (Leitlinie "relative Masse", Selbsttest-Entwurf).
//
// Ausfuehrung in Schritten: Jeder Test ist eine TestSession; ein Aufruf von step() dauert hoechstens ein paar Sekunden (Lesen: 16 MiB, Last: ein
// Messintervall, Schreiben: 4 MiB). So bleibt die UI bedienbar, Abbruch wirkt nach dem Schritt, und jeder Test hat ein hartes Zeitlimit.
// Lasttests starten KEINEN Kindprozess (fork) und beenden ihre Threads selbst nach der Uhr (Lehre vom 11. Nov. 2026, docs/RULES.md).
//
// Absturz-Marker: Vor dem ersten Test steht running.json mit state "running"; beim ordentlichen Ende "done" (ueberschrieben). Findet der
// naechste Start "running", wurde der Lauf durch Absturz, Neustart oder Stromverlust beendet (Befund selftest.interrupted).
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace medic {

constexpr const char* kSelftestDir = "/data/ps5medic/selftest";
constexpr const char* kSelftestMarkerPath = "/data/ps5medic/selftest/running.json";
constexpr const char* kSelftestLastPath = "/data/ps5medic/selftest/last.json";
constexpr const char* kSelftestWritePath = "/data/ps5medic/selftest/write_test.bin";   // die EINE wiederverwendete Testdatei (nie geloescht)
constexpr const char* kSelftestBudgetPath = "/data/ps5medic/selftest/write_budget.json";

constexpr std::uint64_t kReadTestMaxBytes = 128ull << 20;  // Lesetest: hoechstens 128 MiB je Lauf
constexpr int kReadTestMaxSeconds = 15;                    // und hoechstens 15 Sekunden
constexpr int kWriteTestMaxSeconds = 300;                  // Schreibtest (256 MiB schreiben und zuruecklesen): hoechstens fuenf Minuten, dann Abbruch
constexpr std::uint64_t kWriteTestBytes = 256ull << 20;    // Schreibtest: 256 MiB, einmal geschrieben und einmal zurueckgelesen
constexpr std::uint64_t kWriteBudgetBytes = 50ull << 30;   // kumulatives Schreibbudget des Tests (50 GiB)
constexpr std::uint64_t kMemTestBytes = 128ull << 20;     // Speicher-Muster-Test: 128 MiB
constexpr int kThermalCeilingC = 85;                       // Abbruch ab dieser Temperatur (vorlaeufig, nicht geeicht)
constexpr int kThermalStartMaxC = 70;                      // darueber startet der Waermetest nicht

enum class TestStatus { NotRun, Passed, Warn, Failed, Error, Cancelled, Skipped };
const char* to_string(TestStatus s);
TestStatus test_status_from_string(const std::string& s);  // unbekannt: NotRun

// User: Tests, die ein Mensch an der Konsole beurteilt (Pad, Anzeige); sie laufen nie im Ablauf, sondern nur ueber ihren eigenen Bildschirm.
enum class TestTier { Quick, Standard, Thorough, User };

struct TestDef {
    std::string id;
    TestTier tier = TestTier::Quick;
    std::string risk;  // none | read | load | write
    int est_seconds = 1;
    std::string name_de, name_en;
    std::string proves_not_de, proves_not_en;  // "beweist nicht": gehoert in die Ergebnisanzeige
};

struct Measurement {
    std::string name;
    double value = 0;
    std::string unit;
};

struct TestResult {
    std::string id;
    TestStatus status = TestStatus::NotRun;
    std::string reason;  // Kennung bei skipped/cancelled/failed/warn/error (z. B. no_file, read_error, ceiling)
    std::string started; // Zeitstempel "YYYYMMDDTHHMMSSZ"
    double duration_s = 0;
    std::vector<Measurement> measurements;
};

struct TestContext {
    std::function<bool()> cancelled;  // optional: wird zwischen den Schritten abgefragt (run_selftest)
    std::string today;                // "YYYYMMDD" (UTC): Tagesbegrenzung des Schreibtests; leer = Schreibtest wird uebersprungen (no_clock)
    // Messparameter; die Voreinstellung ist der Produktwert, Tests setzen kleinere.
    int thermal_burn_s = 20, thermal_cool_s = 10, step_ms = 1000;  // 13. Nov. 2026: vorher 45 + 30 s
    int net_gap_ms = 150;  // Pause zwischen den Verbindungsversuchen des Netztests (Tests setzen 0)
    std::uint64_t write_bytes = kWriteTestBytes;
    std::uint64_t mem_bytes = kMemTestBytes;  // Speicher-Muster-Test: so viel wird belegt (bei Allokationsfehler halbiert)
    // Nur fuer Tests: einen Rechen- bzw. Speicherfehler vortaeuschen, um die Erkennung zu pruefen.
    bool inject_compute_fault = false, inject_mem_fault = false;
};

// Alle Tests in fester Reihenfolge: billig vor teuer, Schnell vor Standard vor Gruendlich.
const std::vector<TestDef>& selftest_registry();
const TestDef* find_test(const std::string& id);

// Ein laufender Test. step() macht einen Schritt und liefert true, wenn der Test fertig ist; danach result(). abort() beendet ihn nach dem
// laufenden Schritt als cancelled (mit Grund). Der Destruktor beendet Threads und Ressourcen immer.
class TestSession {
public:
    virtual ~TestSession() = default;
    virtual bool step() = 0;
    virtual double progress() const = 0;  // 0 bis 1
    virtual void abort(const std::string& reason) = 0;
    virtual TestResult result() = 0;
};

std::unique_ptr<TestSession> begin_selftest_session(Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx = {});

// Fuehrt einen Test vollstaendig aus (Schleife ueber step(), mit Abbruchabfrage zwischen den Schritten).
TestResult run_selftest(Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx = {});

// Traegt das Ergebnis eines Nutzer-Tests in last.json ein (ersetzt den Eintrag gleicher ID, sonst haengt es an; der Marker bleibt unberuehrt).
bool selftest_record(Platform& p, const std::string& ts, const TestResult& r);

// Marker und Ergebnis (nur mit fs_rw; sonst false). `ids` sind die Tests dieses Laufs.
bool selftest_begin(Platform& p, const std::string& ts, const std::vector<std::string>& ids);
bool selftest_finish(Platform& p, const std::string& ts, const std::vector<TestResult>& results, bool cancelled);

// Letztes gespeichertes Ergebnis (leer, wenn keins oder unlesbar).
std::vector<TestResult> load_last_selftest(const IFileSystem& fs, std::string* finished_ts = nullptr);

// Fakten aus last.json und Marker: test.<id>.status/.reason/.<messwert>, selftest.last_finished, selftest.failed[].id,
// selftest.counts.*, selftest.interrupted.{started,tests[]}.
void collect_selftest(const Platform& p, Facts& f);

}  // namespace medic
