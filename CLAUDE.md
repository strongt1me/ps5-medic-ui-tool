# CLAUDE.md – PS5 Medic

Diese Datei ist der Einstieg für Claude Code. Lies sie zuerst, danach `docs/SPEC.md`, `docs/ARCHITECTURE.md`, `docs/RULES.md`, `docs/ROADMAP.md`.

## Was wir bauen
**PS5 Medic** – eine native **Vollbild-App** für gejailbreakte PS5-Konsolen, die den **Live-Zustand** des Homebrew-Setups prüft (Firmware, HEN, kstuff, ShadowMountPlus, Payloads, Dateien, Configs), bekannte Fehlerursachen in **Klartext** erklärt und – wo sicher möglich – **mit Backup und Undo automatisch repariert**.

Abgrenzung: Es gibt bereits den Windows-„PS5 Jailbreak Doctor“ (MIT, asomimlrxd), der Logs am PC analysiert und bewusst nichts repariert. PS5 Medic läuft **auf der Konsole**, liest den echten Zustand statt nur Logs und **behebt** Probleme. Wir dürfen dessen `knowledge_base.json` (MIT) als Startpunkt übernehmen – mit Namensnennung.

## Entwickler & Umgebung
- Entwickler: Marc (GitHub: strongt1me). Kommuniziere auf Deutsch, kurz und knapp.
- Host: Windows mit **WSL** (Toolchain läuft in WSL), Editor VS Code.
- Test-Konsole: PS5 auf **FW 4.03**, „Unified JB“ über itsPLK WebKit-Autoloader, **kstuff-lite 1.10 beta**, **Payload Manager**, **ShadowMount+**. FTP auf Port **2121**. Port 9021 lauscht nach dem Unified JB **nicht** mehr.
- Vorprojekt mit ähnlicher Technik: `strongt1me/ps5-usb-formatierer` (Payload + Web-UI). Code dort darf als Referenz dienen.

## Tech-Stack (festgelegt)
- **Sprache:** C++20 (Kern) + C wo nötig (SDK-Schnittstellen).
- **PS5-Basis:** [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) (Titel-Generator, UI-Designs, Controller, RADV). Alternative falls Vulkan auf FW 4.03 nicht läuft: SDL2-Software-Rendering wie [Nativehbl](https://github.com/Rufidj/Nativehbl).
- **SDK:** [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (bzw. die vom Template gepinnte Version).
- **Privilegien:** ~~App-Jailbreak über etaHEN-API oder [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon)~~ – unnötig: Als **Payload** (über elfldr/Payload Manager statt als Sony-Title gestartet) liest die App `/data` und `/mnt/*` ohne jede Rechte-Anhebung; das ist seit Phase 2 auf der Konsole bestätigt (Details: `docs/PHASE3.md`, Abschnitt „Korrektur"). **Ausnahme DualSense:** fürs Pad braucht die UI zusätzlich einen App-Kontext (`app_id`), den elfldr-Payloads nicht haben; sie startet deshalb über **websrvs Big-App-Lader** (`/hbldr`, `make launch-ui`, websrv muss auf der Konsole laufen), siehe `docs/PHASE3.md`, „Nachtrag".
- **Regeln:** JSON-Wissensdatenbank (`knowledge/rules.json`), kein hartcodiertes Fehlerwissen im C++-Code.

## Architektur-Leitlinien (wichtig)
1. **Kern ist plattformunabhängig.** Regel-Engine + Fix-Engine laufen auch am PC gegen einen **Snapshot-Ordner** (Fixtures). Dadurch sind fast alle Regeln ohne Konsole testbar – nötig, weil die Test-Konsole nur FW 4.03 hat.
2. **Collector → Facts → Rules → Findings → Fixes.** Collectors sammeln Fakten (read-only), Regeln werten nur Fakten aus, Fixes sind separate, reversible Aktionen.
3. **Jeder Fix ist reversibel:** vorher Backup nach `/data/ps5medic/backups/<timestamp>/`, Journal-Eintrag, Undo-Funktion. Kein Fix ohne Bestätigung durch den Nutzer.
4. **Nie löschen**, nur verschieben (`_to_delete/`) oder umbenennen.
5. **Kein Netz nach aussen** ausser dem optionalen, manuell ausgelösten Regel-Update (SHA-256-geprüft). Umgesetzt als Datei-Update ohne Netz, siehe `docs/PHASE5.md`.

## Grenzen (nicht verhandelbar)
- Keine Exploits, keine Jailbreak-Ketten, keine Firmware-Änderungen.
- Keine Funktionen zum Beschaffen, Entsperren oder Verteilen von Spielen, Keys oder Firmware. Der Medic repariert die **Umgebung**, nicht Inhalte.
- Keine Schreibzugriffe auf `/system`, `/system_ex` oder Flash.

## Build & Test (Zielzustand)
- `make host` → baut `medic-cli` für Linux/WSL, läuft gegen `tests/fixtures/<fall>/`.
- `make test` → Unit- + Regel-Tests gegen alle Fixtures.
- `make ps5` → baut den PS5-Payload (ELF, über elfldr/Payload Manager gestartet – kein Sony-Title mit PPSA-Ordner).
- `make deploy` → per FTP (Port 2121) nach `/data/homebrew/PS5MEDIC/`.
- `make launch-ui` → UI hochladen und über websrv starten; `make dist` → Release-Ordner und Zip unter `dist/`; `medic-cli report <snapshot> --qr` → Bericht am PC.

## Arbeitsweise für Claude Code
- Erst Phase 0 aus `docs/ROADMAP.md` (Host-Kern + Fixtures), **dann** die PS5-Oberfläche.
- Für jede neue Regel: Fixture anlegen, Test schreiben, Regel in `rules.json`, Quelle verlinken.
- Kleine Commits, nach jeder Phase kurzer Status an Marc.
