// SPDX-License-Identifier: GPL-3.0-or-later
// Prueft eine SQLite-Datenbank ganz im Arbeitsspeicher. SQLite ist ohne eigenen Dateizugriff uebersetzt (SQLITE_OS_OTHER): Es kann keine Datei
// oeffnen, aendern oder loeschen. Der Aufrufer liest die Datei (zweimal, zum Vergleich) und uebergibt den Inhalt; die Pruefung arbeitet auf einer Kopie.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace medic {

struct SqliteCheck {
    bool ran = false;   // false: nicht geprueft (zu klein, zu gross, SQLite nicht startbar)
    bool ok = false;    // nur gueltig, wenn ran
    std::string note;   // bei Fehler: erste Meldung von integrity_check, ohne Zeilenumbrueche, hoechstens 200 Zeichen
};

// image: der ganze Inhalt der Datenbankdatei; wal: der Inhalt der WAL-Datei (leer, wenn es keine gibt). Gueltige, abgeschlossene WAL-Eintraege werden
// in die Arbeitskopie eingerechnet, wie SQLite es beim Oeffnen tut; ein WAL mit ungueltigem Kopf ignoriert SQLite ebenfalls. Ein offenes Rollback-Journal
// prueft der Aufrufer vorher (dann wird nicht geprueft).
SqliteCheck sqlite_integrity_check(const std::string& image, const std::string& wal = std::string());

// Kleine, absichtlich beschaedigte Datenbank (synthetisch, ohne echten Inhalt) fuer den Snapshot-Export und Tests.
std::string sqlite_broken_sample();

// Liest die ersten `columns` Spalten einer SELECT-Abfrage als Text (NULL = leer) aus derselben Arbeitskopie im Speicher (nur lesend; die echte Datei
// bleibt unberuehrt). `ran` false: Datenbank oder Abfrage nicht ausfuehrbar (z. B. Tabelle fehlt). Hoechstens `max_rows` Zeilen.
struct SqliteRows {
    bool ran = false;
    bool truncated = false;  // es gab mehr Zeilen als max_rows
    std::vector<std::vector<std::string>> rows;
};
SqliteRows sqlite_select_text(const std::string& image, const std::string& wal, const std::string& sql, int columns, std::size_t max_rows);

// Synthetische Registry-Datenbank (Tabelle tbl_contentinfo mit titleId und metaDataPath) fuer den Snapshot-Export und Tests.
std::string sqlite_make_registry(const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace medic
