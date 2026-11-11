// SPDX-License-Identifier: GPL-3.0-or-later
// Verlauf der Check-ups (/data/ps5medic/history.jsonl): eine JSON-Zeile je Pruefung mit den gemeldeten Regeln. Daraus entstehen
// fuer jeden Befund "neu seit der letzten Pruefung" oder "seit <Tag> (n Pruefungen in Folge)", die Liste der seit der letzten
// Pruefung behobenen Regeln und die Dringlichkeit. Der Verlauf ist ein eigenes Protokoll der App (kein Nutzerdatum): er wird
// nur im eigenen Ordner geschrieben, bei Groesse beiseite gelegt (nie geloescht) und enthaelt keine Pfade, Titel oder Kennungen
// der Konsole, nur Regel-IDs, Schweregrade, Zeit, Firmware und Regelversion.
#pragma once
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <string>
#include <utility>
#include <vector>

namespace medic {

constexpr const char* kHistoryPath = "/data/ps5medic/history.jsonl";
constexpr std::size_t kHistoryMaxBytes = 256 * 1024;  // darueber wird die Datei beiseite gelegt (history_<zeit>.jsonl)
constexpr std::size_t kHistoryKeepRuns = 50;          // so viele juengste Pruefungen bleiben in der neuen Datei
constexpr std::size_t kHistoryMaxRuns = 200;          // so viele juengste Pruefungen werden gelesen

struct HistoryRun {
    std::string ts;             // "YYYYMMDDTHHMMSSZ"
    std::string rules_version;
    std::string fw;
    std::vector<std::pair<std::string, std::string>> findings;  // Regel-ID, Schweregrad
};

// Liest den Verlauf (aelteste zuerst, hoechstens kHistoryMaxRuns). Kaputte Zeilen werden uebersprungen; ohne Datei leer.
std::vector<HistoryRun> load_history(const IFileSystem& fs);

HistoryRun make_history_run(const std::string& ts, const std::string& rules_version, const std::string& fw, const std::vector<Finding>& findings);

// Setzt status ("new" | "recurring"), first_seen und seen_runs der Befunde gegen die letzte Pruefung im Verlauf und liefert die
// Regel-IDs, die dort noch gemeldet wurden und jetzt fehlen. Ohne Verlauf bleibt alles leer (es gibt keinen Vergleich).
std::vector<std::string> annotate_with_history(std::vector<Finding>& findings, const std::vector<HistoryRun>& history, const std::string& now_ts);

// Haengt die Pruefung an (ohne fs_rw oder ohne gueltigen Zeitstempel: false). Ueber kHistoryMaxBytes wird die Datei nach
// /data/ps5medic/history_<zeit>.jsonl verschoben und die neue mit den juengsten kHistoryKeepRuns Pruefungen begonnen.
bool append_history(Platform& p, const HistoryRun& run);

// Dringlichkeit "high" | "medium" | "low": Schweregrad (kritisch hoch, Warnung mittel, Hinweis niedrig); ein Befund, der seit mindestens
// drei Pruefungen in Folge gemeldet wird und dessen Verlaesslichkeit nicht niedrig ist, rueckt eine Stufe nach oben.
// Die Dringlichkeit ordnet nur die Anzeige; sie loest nie eine Reparatur aus.
std::string urgency_of(const Finding& f);

}  // namespace medic
