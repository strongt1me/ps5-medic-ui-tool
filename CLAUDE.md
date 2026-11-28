# CLAUDE.md – PS5 Medic

Diese Datei ist der Einstieg für Claude Code. Lies sie zuerst, danach `docs/SPEC.md`, `docs/ARCHITECTURE.md`, `docs/RULES.md`, `docs/ROADMAP.md`.

## Was wir bauen
**PS5 Medic** – eine native **Vollbild-App** für gejailbreakte PS5-Konsolen, die den **Live-Zustand** des Homebrew-Setups prüft (Firmware, HEN, kstuff, ShadowMountPlus, Payloads, Dateien, Configs), bekannte Fehlerursachen in **Klartext** erklärt und – wo sicher möglich – **mit Backup und Undo automatisch repariert**.

Abgrenzung: Es gibt bereits den Windows-„PS5 Jailbreak Doctor“ (MIT, asomimlrxd), der Logs am PC analysiert und bewusst nichts repariert. PS5 Medic läuft **auf der Konsole**, liest den echten Zustand statt nur Logs und **behebt** Probleme. Wir dürfen dessen `knowledge_base.json` (MIT) als Startpunkt übernehmen – mit Namensnennung.

## Entwickler & Umgebung
- **Sprachen der App (Marc, 12. Nov. 2026):** Deutsch, Englisch, Französisch, Italienisch, Spanisch (`ui/i18n/`, `knowledge/l10n/`). Die App läuft auf den Firmwares **1.00 bis 14.22** (README und App-Info); getestet ist nur FW 12.00.
- Entwickler: Marc (GitHub: strongt1me). Kommuniziere auf Deutsch, kurz und knapp.
- Host: Windows mit **WSL** (Toolchain läuft in WSL), Editor VS Code.
- Test-Konsolen (Marc, 14. Nov. 2026), beide auf **FW 12.00**, FTP auf Port **2121**, ELF-Lader auf Port **9021** offen: **192.168.1.94** = PS5 Pro (CFI-7021) mit **kstuff**, **Payload Manager 0.6.3**, **ShadowMount+ 1.7beta12**; **192.168.1.52** = PS5 (CFI-2116). Beim Senden immer sagen, an welche Adresse (`make send-launcher PS5_HOST=<Adresse>`).
- Vorprojekt mit ähnlicher Technik: `strongt1me/ps5-usb-formatierer` (Payload + Web-UI). Code dort darf als Referenz dienen.

## Tech-Stack (festgelegt)
- **Sprache:** C++20 (Kern) + C wo nötig (SDK-Schnittstellen).
- **PS5-Basis:** [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) (Titel-Generator, UI-Designs, Controller, RADV). Alternative falls Vulkan nicht läuft: SDL2-Software-Rendering wie [Nativehbl](https://github.com/Rufidj/Nativehbl).
- **SDK:** [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (bzw. die vom Template gepinnte Version).
- **Oberfläche (seit 12. Nov. 2026):** native App (Titel `FAKE42007`, `ps5/native_ui_main.cpp`) als reine Anzeige mit Pad; der Dienst-Payload `ps5medicservice.elf` (`ps5/service_main.cpp`) zeichnet die UI und liest die Konsole, Verbindung über `127.0.0.1:9060`. Grund: in der geliehenen Big App `FAKE00000` ging die PS-Taste nicht. Einzelheiten: `docs/NATIVE.md`. Der folgende Absatz beschreibt den älteren Weg.
- **Privilegien:** ~~App-Jailbreak über etaHEN-API oder [PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon)~~ – unnötig: Als **Payload** (über elfldr/Payload Manager statt als Sony-Title gestartet) liest die App `/data` und `/mnt/*` ohne jede Rechte-Anhebung; das ist seit Phase 2 auf der Konsole bestätigt (Details: `docs/PHASE3.md`, Abschnitt „Korrektur"). **Ausnahme DualSense:** fürs Pad braucht die UI zusätzlich einen App-Kontext (`app_id`), den elfldr-Payloads nicht haben; sie startet deshalb über den **Big-App-Lader aus websrv**, der im Launcher steckt (nur der Code, kein websrv nötig). Die Kachel („PS5 Medic", Ordner-App `FAKE42007` unter `/data/homebrew/`, ShadowMount+ legt die Kachel unter „Medien" an; das Senden von `ps5medic-launcher.elf` legt den Ordner selbst an) ist ein nativer Starter-Titel, der den Launcher an Port 9021 sendet; Apps und Spiele liegen bei Marc immer in `/data/homebrew/`, nie unter `/system_ex` (Marc, 12. Nov. 2026); gebaut mit **ps5-payload-sdk 0.50** (Marc, 12. Nov. 2026). Siehe `docs/TITLE.md`.
- **Regeln:** JSON-Wissensdatenbank (`knowledge/rules.json`), kein hartcodiertes Fehlerwissen im C++-Code.

## Architektur-Leitlinien (wichtig)
1. **Kern ist plattformunabhängig.** Regel-Engine + Fix-Engine laufen auch am PC gegen einen **Snapshot-Ordner** (Fixtures). Dadurch sind fast alle Regeln ohne Konsole testbar – nötig, weil die Test-Konsolen nur FW 12.00 haben.
2. **Collector → Facts → Rules → Findings → Fixes.** Collectors sammeln Fakten (read-only), Regeln werten nur Fakten aus, Fixes sind separate, reversible Aktionen.
3. **Jeder Fix ist reversibel:** vorher Backup nach `/data/ps5medic/backups/<timestamp>/`, Journal-Eintrag, Undo-Funktion. Kein Fix ohne Bestätigung durch den Nutzer.
4. **Nie löschen**, nur verschieben (`_to_delete/`) oder umbenennen. Einzige Ausnahme (Marc, 28. Nov. 2026): die eigenen Check-ups von PS5 Medic (`.json` in `/data/ps5medic/checks/`) dürfen echt gelöscht werden, im Verlauf auf Wunsch des Nutzers mit Rückfrage und beim Speichern automatisch bis auf die neuesten 50; nur über `remove_check_file`, `history.jsonl` bleibt.
5. **Kein Netz nach aussen** ausser dem optionalen, manuell ausgelösten Regel-Download: standardmäßig `https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/rules-update/` (GitHub, Marc, 18. Nov. 2026; TLS über das eingebaute BearSSL mit fest eingebauten Wurzelzertifikaten), oder die Adresse `update_url` (`http://` oder `https://`) aus `/data/ps5medic/config.ini`; nur nach Bestätigung, nur zwei GET-Abrufe (`rules.json`, `rules.json.sig`), keine Weiterleitungen, angenommen nur mit gültiger Ed25519-Signatur (Schlüssel eingebaut, Privatschlüssel nie im Repo). Daneben bleibt das Datei-Update (SHA-256) ohne Netz. Siehe `docs/PHASE5.md`. Einzige weitere Ausnahme (Marc, 11. Nov. 2026): der lokale Netztest des Selbsttests, nur TCP-Verbindungsaufbau zum Standard-Gateway im privaten Adressbereich, keine Daten, nie das Internet (`docs/RULES.md`). Dazu (Marc, 28. Nov. 2026) lesende Abfragen an Kraken Mount auf derselben Konsole: nur `GET` an `127.0.0.1:8097`, nur `/api/state` und `/api/ampr`, nie `POST` oder andere Pfade (`local_api_get` in `platform.hpp`).

## Grenzen (nicht verhandelbar)
- Keine Exploits, keine Jailbreak-Ketten, keine Firmware-Änderungen.
- Keine Funktionen zum Beschaffen, Entsperren oder Verteilen von Spielen, Keys oder Firmware. Der Medic repariert die **Umgebung**, nicht Inhalte.
- Keine Schreibzugriffe auf `/system`, `/system_ex` oder Flash.
- **Nie ein Update der Systemsoftware/Firmware anzeigen oder dazu raten**, bei keinem Fehler und in keinem Text (Marc, 10. Nov. 2026). Erzwingt `libmedic/src/text_policy.cpp` (Lader + Tests); Einzelheiten in `docs/RULES.md`.

## Lizenz & Sichtbarkeit (Entscheidung von Marc, 8. Nov. 2026)
- Lizenz: **GPL-3.0-or-later** (`LICENSE`). Jede neue eigene Quelldatei bekommt als erste Zeile (nach einer Shebang) `SPDX-License-Identifier: GPL-3.0-or-later` (`//` bzw. `#`). Nicht für `third_party/`.
- Das Repository war zuerst **privat** (8. Nov. 2026); **Marc hat am 16. Nov. 2026 entschieden, es öffentlich zu stellen** und `v0.4.0` als Latest-Release zu veröffentlichen (die früheren Pre-Releases `v0.4.0-dev.2` und `-dev.3` werden vorher entfernt). Keine Forenbeiträge ohne seine Anweisung, weitere Releases nur auf seine Anweisung. Echte Dateinamen aus Marcs Sammlung trotzdem nie in Fixtures, Tests oder Doku.

## Build & Test (Zielzustand)
- `make host` → baut `medic-cli` für Linux/WSL, läuft gegen `tests/fixtures/<fall>/`.
- `make test` → Unit- + Regel-Tests gegen alle Fixtures. Läuft auch auf GitHub bei jedem Pull Request und jedem Push auf `main` (`.github/workflows/ci.yml`, ubuntu-24.04, ohne PS5-SDK).
- `make ps5` → baut den PS5-Payload (ELF, über elfldr/Payload Manager gestartet – kein Sony-Title mit PPSA-Ordner).
- `make deploy` → per FTP (Port 2121) nach `/data/homebrew/PS5MEDIC/`.
- `make send-launcher` → Launcher senden (Kachel + UI); `make title` → Ordner-App `FAKE42007`; `make launch-ui` → Ausweichweg über websrv; `make dist` → Release-Ordner und Zip unter `dist/`; `medic-cli report <snapshot> --qr` → Bericht am PC.

## Arbeitsweise für Claude Code
- Erst Phase 0 aus `docs/ROADMAP.md` (Host-Kern + Fixtures), **dann** die PS5-Oberfläche.
- Für jede neue Regel: Fixture anlegen, Test schreiben, Regel in `rules.json`, Quelle verlinken.
- Kleine Commits, nach jeder Phase kurzer Status an Marc.
- **Keine Testspiele in Scanpfaden der Konsole** (Marc, 15. Nov. 2026): ShadowMount+ hängt jeden Ordner mit `eboot.bin` und `param.json` aus `/data/homebrew` usw. ein; ihn dort per FTP zu löschen stürzte die Konsole ab. Tests ohne Spielmarken oder außerhalb der Scanpfade machen, vorher ShadowMount+ anhalten, nichts im Scanpfad löschen.
