# PS5 Medic – Bauplan für Claude Code

Jede Phase endet mit lauffähigem Stand + kurzem Status an Marc.

## Phase 0 – Host-Kern (nur PC/WSL, ohne Konsole)
1. Repo-Gerüst: `libmedic/`, `cli/`, `app/`, `knowledge/`, `tests/fixtures/`, CMake, `Makefile`.
2. `IFileSystem`, `IProcessList`, `ISystemInfo` + `SnapshotPlatform` (liest `tests/fixtures/<fall>/fs/…` und `procs.json`, `system.json`).
3. Fakten-Store + Collectors `shadowmount`, `processes`, `firmware`, `storage`.
4. Regel-Engine (JSON laden, Schema prüfen, Bedingungen: `eq`, `lt/gt` mit Versionsvergleich, `matches`, `exists`, `contains_line`).
5. Fixtures + Tests für Regeln **B1–B15** und **A1, A3**.
6. `medic-cli check tests/fixtures/<fall>` gibt Befunde als Text/JSON aus.

**Fertig, wenn:** `make test` grün, alle Regeln haben ein Positiv- und ein Negativ-Fixture.

## Phase 1 – Fix-Engine + Journal
1. `plan/apply/undo` für die 5 MVP-Fixes.
2. Pfad-Whitelist, Backups, `journal.jsonl` mit SHA-256 vorher/nachher.
3. Tests: apply → undo → Dateibaum identisch.

## Phase 2 – PS5-Plattform (Konsole FW 4.03)
> Stand: Schritte 2 und 3 sind als Diagnose-Payload vorbereitet und bauen (`make ps5`), Schritt 1 und der Lauf auf der Konsole stehen aus – siehe `docs/PHASE2.md`.
1. **Erst prüfen:** Läuft ein Sample aus PS5_VulkanTemplate auf FW 4.03? Falls nein → SDL2-Software-Rendering (Plan B).
2. `Ps5Platform`: Firmware, Prozessliste, Dateizugriff mit App-Jailbreak (etaHEN-API oder Lapy-JB-Daemon).
3. **Snapshot-Export auf der Konsole:** Medic kann den eigenen Zustand als Fixture-Ordner nach `/data/ps5medic/snapshots/` schreiben → per FTP holen → am PC testen.

## Phase 3 – Vollbild-UI
1. Titel mit `new-title.py` aus dem Template erzeugen, Design wählen.
2. Screens: Start, Check-up läuft, Befundliste, Detail, Reparieren (Vorschau → Bestätigen), Verlauf/Undo, Einstellungen (Sprache).
3. Controller-Belegung laut SPEC, Strings in `i18n/de.json` und `en.json`.

## Phase 4 – Bericht & Release
1. Markdown-Bericht + Anonymisierung + QR-Code.
2. `param.json`, Icon, PPSA-Ordner; Installation per FTP nach `/data/homebrew/`.
3. README (DE/EN), `THIRD_PARTY.md`, GitHub-Release `strongt1me/ps5-medic`.

## Phase 5 – Community
1. Regel-Update (manuell, SHA-256).
2. „Unbekannte Fehlerzeilen“ sammeln → Issue-Vorlage.
3. Tester mit FW 7.x–13.60 suchen (GBAtemp, Discord), Fixtures einsammeln.

## Erster Prompt für Claude Code
> Lies CLAUDE.md und docs/*. Starte mit Phase 0, Schritte 1–3. Lege für Regel B2 und B8 je ein Positiv- und Negativ-Fixture an und zeige mir danach die Ausgabe von `medic-cli check` für alle Fixtures.
