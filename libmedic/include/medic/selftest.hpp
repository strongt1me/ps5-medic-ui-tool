// SPDX-License-Identifier: GPL-3.0-or-later
// Selbsttest-Framework (Batch 7, docs/DIAGNOSE.md). Ein Test ist ein aktiver Collector: Er liefert ein Ergebnis mit Status und Messwerten,
// das als Fakten `test.<id>.*` in dieselbe Regel-Engine fliesst. Alles hier ist NUR LESEND (Risiko "none" oder "read"); geschrieben wird
// ausschliesslich unter /data/ps5medic/selftest/ (Marker und letztes Ergebnis, immer ueberschrieben, nie geloescht).
//
// Statusmodell: passed (nicht aufgefallen, nie "gesund"), warn, failed, error (Test selbst gescheitert), cancelled (Abbruch, mit Grund),
// skipped (Vorbedingung fehlt, nie passed), not_run (noch nicht gelaufen). Faellt eine Vorbedingung aus, ist der Test skipped, nie failed.
// Ohne Baseline urteilen Messwert-Tests nicht ueber Zahlen: sie speichern Rohwerte (Leitlinie "relative Masse", Selbsttest-Entwurf).
//
// Absturz-Marker: Vor dem ersten Test steht running.json mit state "running"; beim ordentlichen Ende "done" (ueberschrieben). Findet der
// naechste Start "running", wurde der Lauf durch Absturz, Neustart oder Stromverlust beendet (Befund selftest.interrupted).
#pragma once
#include "medic/facts.hpp"
#include "medic/platform.hpp"

#include <functional>
#include <string>
#include <vector>

namespace medic {

constexpr const char* kSelftestDir = "/data/ps5medic/selftest";
constexpr const char* kSelftestMarkerPath = "/data/ps5medic/selftest/running.json";
constexpr const char* kSelftestLastPath = "/data/ps5medic/selftest/last.json";

constexpr std::uint64_t kReadTestMaxBytes = 128ull << 20;  // hoechstens 128 MiB je Lauf
constexpr int kReadTestMaxSeconds = 15;                    // und hoechstens 15 Sekunden

enum class TestStatus { NotRun, Passed, Warn, Failed, Error, Cancelled, Skipped };
const char* to_string(TestStatus s);
TestStatus test_status_from_string(const std::string& s);  // unbekannt: NotRun

enum class TestTier { Quick, Standard };

struct TestDef {
    std::string id;
    TestTier tier = TestTier::Quick;
    std::string risk;  // none | read
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
    std::string reason;  // Kennung bei skipped/cancelled/failed/error (z. B. no_file, read_error, cancelled)
    std::string started; // Zeitstempel "YYYYMMDDTHHMMSSZ"
    double duration_s = 0;
    std::vector<Measurement> measurements;
};

struct TestContext {
    std::function<bool()> cancelled;  // optional: wird zwischen Lesebloecken abgefragt
};

// Alle Tests in fester Reihenfolge: billig vor teuer, Schnell vor Standard.
const std::vector<TestDef>& selftest_registry();
const TestDef* find_test(const std::string& id);

// Fuehrt EINEN Test aus. `facts` sind die Fakten eines frischen collect_all (passive Daten); der Test liest nur.
TestResult run_selftest(const Platform& p, const Facts& facts, const TestDef& def, const TestContext& ctx = {});

// Marker und Ergebnis (nur mit fs_rw; sonst false). `ids` sind die Tests dieses Laufs.
bool selftest_begin(Platform& p, const std::string& ts, const std::vector<std::string>& ids);
bool selftest_finish(Platform& p, const std::string& ts, const std::vector<TestResult>& results, bool cancelled);

// Letztes gespeichertes Ergebnis (leer, wenn keins oder unlesbar).
std::vector<TestResult> load_last_selftest(const IFileSystem& fs, std::string* finished_ts = nullptr);

// Fakten aus last.json und Marker: test.<id>.status/.reason/.<messwert>, selftest.last_finished, selftest.failed[].id,
// selftest.counts.*, selftest.interrupted.{started,tests[]}.
void collect_selftest(const Platform& p, Facts& f);

}  // namespace medic
