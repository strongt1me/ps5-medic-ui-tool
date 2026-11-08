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
> Stand: erledigt. Diagnose-Payload gebaut (`make ps5`) und am 8. Nov. auf der Konsole gelaufen (FW 12.00, nicht 4.03 – siehe `docs/PHASE2.md`). `Ps5Platform` liest `/data` und `/mnt/*` frei, **ohne** App-Jailbreak: Payloads über elfldr/Payload Manager sind grundsätzlich nicht sandboxed, der Jailbreak-Request war eine falsche Annahme (siehe `docs/PHASE3.md`, Abschnitt „Korrektur"). Schritt 1 (Grafik-Sample) steht noch aus.
1. **Erst prüfen:** Läuft ein Sample aus PS5_VulkanTemplate auf FW 4.03? Falls nein → SDL2-Software-Rendering (Plan B).
2. `Ps5Platform`: Firmware, Prozessliste, Dateizugriff – als Payload, ohne Rechte-Anhebung.
3. **Snapshot-Export auf der Konsole:** Medic kann den eigenen Zustand als Fixture-Ordner nach `/data/ps5medic/snapshots/` schreiben → per FTP holen → am PC testen.

## Phase 3 – Vollbild-UI
> Stand: Der plattformunabhängige Teil (Canvas, Schrift, de/en-Texte, neun Bildschirme, Service-Schnittstelle, PNG-Vorschau) steht und ist getestet. PS5 Medic wird als Payload gebaut, nicht als Sony-Title – kein Jailbreak-Request nötig. `make ps5-ui` baut `ps5medicui.elf` mit SDL2 (VideoOut/Pad) und `LocalService`. **Phase 3 ist erledigt:** Auf der Konsole läuft die UI, wenn websrvs Big-App-Lader sie startet (`make launch-ui`; nur mit App-Kontext öffnet sich das Pad). Der ganze Ablauf (Check-up, Reparatur, Rückgängig) ist am 8. Nov. auf der Test-Konsole durchgespielt – siehe `docs/PHASE3.md`.
1. ~~Titel mit `new-title.py` aus dem Template erzeugen~~ entfällt (kein Sony-Title, siehe `docs/PHASE3.md`).
2. Screens: Start, Check-up läuft, Befundliste, Detail, Reparieren (Vorschau → Bestätigen), Verlauf/Undo, Einstellungen (Sprache). – erledigt.
3. Controller-Belegung laut SPEC, Strings in `i18n/de.json` und `en.json`. – erledigt.

## Phase 4 – Bericht & Release
> Stand: Bericht, Anonymisierung, QR-Code, □-Taste, READMEs und `make dist` sind fertig und getestet (siehe `docs/PHASE4.md`). Lizenz GPL-3.0-or-later; kein öffentliches Release, das Repository bleibt privat (Entscheidung vom 8. Nov. 2026).
1. Markdown-Bericht + Anonymisierung + QR-Code. – erledigt.
2. ~~`param.json`, Icon, PPSA-Ordner~~ entfällt: Launcher-Eintrag `ps5/homebrew/` (siehe `docs/PHASE3.md`). Installation per FTP nach `/data/homebrew/PS5MEDIC/`.
3. README (DE/EN), `THIRD_PARTY.md` – erledigt. GitHub-Release – entfällt (Repository privat).

## Phase 5 – Community
> Stand: Regel-Update (als Datei-Update ohne Netz), unbekannte Fehlerzeilen, Issue-Vorlage und Tester-Anleitung sind fertig und getestet (siehe `docs/PHASE5.md`). Foren-Beiträge und GitHub-Release entfallen (Repository privat). Der signierte Regel-Download kommt als Nächstes.
1. Regel-Update (manuell, SHA-256). – erledigt (Datei auf die Konsole legen, in PS5 Medic prüfen und installieren; kein Download, Begründung in `docs/PHASE5.md`).
2. „Unbekannte Fehlerzeilen“ sammeln → Issue-Vorlage. – erledigt (Abschnitt im Bericht, `.github/ISSUE_TEMPLATE/problem.md`).
3. Tester mit FW 7.x–13.60 suchen (GBAtemp, Discord), Fixtures einsammeln. – Anleitung und Entwurf des Aufrufs in `docs/TESTER.md`; wird nicht veröffentlicht (Repository privat), Tester nur persönlich.

## Erster Prompt für Claude Code
> Lies CLAUDE.md und docs/*. Starte mit Phase 0, Schritte 1–3. Lege für Regel B2 und B8 je ein Positiv- und Negativ-Fixture an und zeige mir danach die Ausgabe von `medic-cli check` für alle Fixtures.
