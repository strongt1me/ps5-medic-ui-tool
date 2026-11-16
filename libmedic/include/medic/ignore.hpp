// SPDX-License-Identifier: GPL-3.0-or-later
// Befunde ausblenden: Warnungen und Hinweise, die man nicht selbst beheben kann, muessen nicht bei jedem Check-up wieder oben stehen.
// Nur die Anzeige aendert sich: gespeicherte Check-ups, Verlauf, Bericht und Support-Paket enthalten weiter alle Befunde. Gespeichert wird je Regel
// in /data/ps5medic/ignored.json (eigene Datei der App); ein Eintrag wird nie geloescht, nur auf "inaktiv" gesetzt (Wieder anzeigen).
//  * Kritische Befunde lassen sich nicht ausblenden.
//  * Steigt der Schweregrad einer ausgeblendeten Regel (Hinweis -> Warnung -> kritisch), erscheint sie wieder: ausgeblendet gilt nur fuer den
//    Schweregrad, bei dem man sie ausgeblendet hat, und darunter.
#pragma once
#include "medic/platform.hpp"
#include "medic/rules.hpp"

#include <string>
#include <vector>

namespace medic {

constexpr const char* kIgnorePath = "/data/ps5medic/ignored.json";
constexpr std::size_t kIgnoreMaxEntries = 500;  // mehr werden nicht gelesen (so viele Regeln gibt es nicht)

struct IgnoreEntry {
    std::string rule_id;
    Severity severity = Severity::Info;  // Schweregrad beim Ausblenden
    std::string ts;                      // Zeit des Ausblendens bzw. der letzten Aenderung ("YYYYMMDDTHHMMSSZ")
    bool active = true;                  // false = wieder angezeigt (der Eintrag bleibt als Spur)
};

// Liest die Datei; ohne Datei oder bei kaputtem Inhalt leer (nichts wird ausgeblendet).
std::vector<IgnoreEntry> load_ignored(const IFileSystem& fs);

// Schreibt die Liste (ohne fs_rw: false).
bool save_ignored(Platform& p, const std::vector<IgnoreEntry>& entries);

// Kritische Befunde bleiben immer sichtbar.
bool can_hide(Severity s);

// true, wenn der Befund ausgeblendet ist: aktiver Eintrag fuer die Regel, nicht kritisch, und der Schweregrad ist nicht hoeher als beim Ausblenden.
bool is_hidden(const std::string& rule_id, Severity now, const std::vector<IgnoreEntry>& entries);

// Neue Liste mit dem Befund ausgeblendet (hide = true; kritisch wird abgelehnt: unveraendert zurueck) bzw. wieder angezeigt.
std::vector<IgnoreEntry> set_hidden(std::vector<IgnoreEntry> entries, const std::string& rule_id, Severity sev, const std::string& ts, bool hide);

}  // namespace medic
