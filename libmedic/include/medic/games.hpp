// SPDX-License-Identifier: GPL-3.0-or-later
// Ansicht "Pro Spiel": Fasst aus den Fakten und Befunden des letzten Check-ups je Titel-ID zusammen, was Medic ueber das Spiel weiss und
// was einen Start verhindern koennte (Quellen, Registrierung, verlangte Firmware, Fehlerverlauf der Konsole, Spielstaende, SM+-Log, Befunde).
// Es wird nichts neu gesammelt und nichts geschrieben; nur was die Collectors gelesen haben, steht hier. Ein Fehlen von Spuren heisst nie
// "in Ordnung" (docs/DIAGNOSE.md, Leitregel 2): "unbekannt" bleibt "unbekannt".
#pragma once
#include "medic/facts.hpp"
#include "medic/rules.hpp"

#include <string>
#include <vector>

namespace medic {

struct GameSource {
    std::string path, root;
    std::string kind;       // "folder" | "image"
    bool active = false;    // diese Quelle nutzt ShadowMount+ gerade
    int dup_count = 1;      // so viele Quellen mit derselben TITLE_ID
};

struct GameError {
    std::string code;  // z. B. "CE-108262-9"
    int count = 0;
    std::string last;  // Tag der letzten Meldung ("YYYY-MM-DD"), leer wenn unbekannt
};

struct GameInfo {
    std::string titleid, name;
    std::vector<GameSource> sources;
    int installed = -1;   // 1: Ordner unter /user/app, 0: nicht, -1: nicht gelesen
    int registered = -1;  // Systemdatenbank kennt den Titel: 1 ja, 0 nein (Ordner ohne Eintrag), -1 nicht gelesen
    bool orphan = false;  // Eintrag in der Systemdatenbank ohne Ordner
    std::string required_fw, sdk_fw;
    int backport = -1;    // Backport-Ordner vorhanden: 1 ja, 0 nein, -1 unbekannt (keine Firmware-Angabe)
    bool needs_backport = false;  // verlangt neuere Firmware als die Konsole, ohne Backport
    bool eboot_bad = false, case_mismatch = false, oversized = false;
    std::vector<GameError> errors;
    int foreign_saves = 0;                 // Spielstaende mit anderer Konto-ID
    std::vector<std::string> log_lines;    // letzte Zeilen des ShadowMount+-Logs, die die TITLE_ID nennen (hoechstens kGameLogLines)
    std::vector<std::size_t> findings;     // Indizes in die Befundliste, die diesen Titel betreffen (Reihenfolge der Befundliste)
    Severity worst = Severity::Info;       // schlimmster Schweregrad dieser Befunde (nur gueltig, wenn findings nicht leer)
};

constexpr std::size_t kGameLogLines = 4;
constexpr std::size_t kMaxGames = 200;

// Alle Titel, zu denen die Fakten oder Befunde etwas sagen. Sortiert: Titel mit Befunden zuerst (schlimmster zuerst), dann nach TITLE_ID.
std::vector<GameInfo> collect_game_infos(const Facts& facts, const std::vector<Finding>& findings);

}  // namespace medic
