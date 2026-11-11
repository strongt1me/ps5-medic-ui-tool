# SQLite 3.54.1 (Amalgamation)

Quelle: https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip (SHA3-256 92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30).
Lizenz: gemeinfrei (Public Domain, https://www.sqlite.org/copyright.html). Unveraendert.

PS5 Medic uebersetzt SQLite ohne Dateizugriff (`SQLITE_OS_OTHER`, siehe `libmedic/src/sqlite_check.cpp`) und nutzt es nur, um die Datenbanken der
Systemsoftware im Arbeitsspeicher zu pruefen (`PRAGMA integrity_check` auf einer Kopie). Es oeffnet, aendert oder schreibt nie eine echte Datenbank.
