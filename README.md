# PS5 Medic

*[English version](README.en.md)*

**PS5 Medic** prüft auf einer gejailbreakten PS5 den **Live-Zustand** des Homebrew-Setups (Firmware, Jailbreak-Kette, kstuff, ShadowMount+, Payloads, Dateien, Konfiguration), erklärt bekannte Fehlerursachen **in Klartext** und repariert – wo das sicher möglich ist – **mit Backup und Rückgängig-Funktion**.

Läuft als Vollbild-App direkt auf der Konsole und wird mit dem DualSense bedient. Ein Knopfdruck: *Check-up starten*.

> Stand: Version 0.4.0-dev. Getestet auf einer PS5 Pro (FW 12.00) mit ShadowMount+ 1.7beta11 und websrv v0.33. Andere Firmwares und Setups sind nicht getestet; die Regeln sind deshalb versionsgebunden formuliert und einzeln als *bestätigt* oder *noch nicht bestätigt* gekennzeichnet.

## Was es kann

- **Check-up**: 22 Regeln (Jailbreak-Kette und HEN, ShadowMount+, Plugins und Speicher), Ergebnis als Liste mit Schweregrad, Klartext, Evidenz (Datei/Zeile/Wert) und Quelle. Der Katalog steht in [docs/RULES.md](docs/RULES.md).
- **Reparieren** (5 Fixes): Image umbenennen (Klammern), ShadowMount+-Einstellung setzen, BackPork aus der Autoload-Liste nehmen, doppelte Kopie derselben TITLE_ID verschieben, Anleitung bei verschachtelten Images. Vorher zeigt die Vorschau genau, was geändert wird.
- **Rückgängig**: Jede Reparatur steht im Verlauf und lässt sich zurücknehmen.
- **Bericht**: Taste □ in der Befundliste schreibt einen Markdown-Bericht nach `/data/ps5medic/reports/` und zeigt eine Kurzfassung als **QR-Code** (mit dem Handy scannen, Text kopieren, ins Forum oder in ein Issue einfügen). IP- und MAC-Adressen, Benutzer- und Konto-IDs sowie Seriennummern werden entfernt; Datei- und Ordnernamen bleiben drin, bitte vor dem Teilen prüfen.
- Deutsch und Englisch (Einstellungen).

## Sicherheit

- **Nie löschen**, nur verschieben oder umbenennen. Dateien, die geändert werden, werden vorher gesichert (`/data/ps5medic/backups/`), jede Änderung steht im Journal (`/data/ps5medic/journal.jsonl`).
- **Kein Schreiben ohne ✕**: Reparatur und Rückgängig passieren erst nach ausdrücklicher Bestätigung auf der Vorschau. Die einzige weitere Schreibaktion ist der Bericht (□).
- Schreibzugriffe nur in einer Whitelist (ShadowMount+-Ordner, Autoload-Listen, die eigenen Scanpfade, `/data/ps5medic`). Nie unter `/system`, `/system_ex` oder `/dev`.
- **Kein Netzwerkzugriff nach außen.** (Der Test, ob der ELF-Loader auf Port 9021 antwortet, geht nur an `127.0.0.1`.)
- Keine Exploits, keine Firmware-Änderungen, keine Funktionen zum Beschaffen, Entsperren oder Verteilen von Spielen, Keys oder Firmware. PS5 Medic repariert die **Umgebung**, nicht Inhalte.

## Voraussetzungen

- Eine bereits gejailbreakte PS5 mit ELF-Loader (Port 9021) und FTP (hier Port 2121).
- Für den Start mit DualSense: [websrv](https://github.com/ps5-payload-dev/websrv) mit Homebrew-Launcher (getestet: v0.33). Nur über dessen „Big App"-Lader bekommt ein Payload den App-Kontext, den die Konsole zum Öffnen des Controllers verlangt (Hintergrund: [docs/PHASE3.md](docs/PHASE3.md)). Ohne websrv läuft nur der Diagnose-Payload (`ps5medic.elf`).

## Installation

1. Den Ordner `PS5MEDIC` aus dem Release per FTP nach `/data/homebrew/` kopieren (er enthält `ps5medicui.elf`, `ps5medic.elf`, `rules.json`, `homebrew.js` und das Icon).
2. `websrv` starten (Payload Manager, Autoloader oder Port 9021).
3. Im Launcher (`http://<PS5-IP>:8080`) die Kachel **PS5 Medic** öffnen. Alternativ vom PC: `make launch-ui PS5_HOST=<PS5-IP>`.

Wichtig: `websrv` beendet beim Start einer Homebrew-App eine laufende Big App, also auch ein Spiel.

## Bedienung

| Bildschirm | Tasten |
|---|---|
| Start | ↑↓ wählen · ✕ öffnen · ◯ beenden |
| Befundliste | ↑↓ wählen · ✕ Details · △ Reparieren/Anleitung · □ Bericht · ◯ zurück |
| Detail | △ Reparieren · ◯ zurück |
| Reparatur-Vorschau | ✕ bestätigen (schreibt) · ◯ abbrechen |
| Verlauf | ↑↓ wählen · ✕ rückgängig (fragt erst nach) · ◯ zurück |
| Bericht | ◯ zurück |

## Diagnose-Payload

`ps5medic.elf` läuft ohne Oberfläche, **nur lesend**: Er schreibt `/data/ps5medic/last_check.txt` und exportiert den Zustand als Snapshot nach `/data/ps5medic/snapshots/`. Den Snapshot kann man am PC mit `medic-cli check` prüfen (Fixture-Ordner, siehe unten).

## Aus dem Quelltext bauen

Linux oder WSL, CMake, ein C++20-Compiler. Der Kern und die Regeln laufen auch am PC gegen Fixture-Ordner.

```bash
make test        # Unit-, Regel-, UI- und Berichts-Tests gegen alle Fixtures
make host        # medic-cli und die PC-Vorschau der UI
```

Für die Konsole braucht man das [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (`PS5_PAYLOAD_SDK`) und SDL2 für die PS5; die Bauanleitung dafür steht in [docs/PHASE3.md](docs/PHASE3.md).

```bash
make ps5-ui                         # Vollbild-UI (ps5medicui.elf)
make ps5                            # Diagnose-Payload (ps5medic.elf)
make launch-ui PS5_HOST=192.168.1.x # hochladen und über websrv starten
make dist                           # Release-Ordner und Zip unter dist/ (nur lokal)
```

Werkzeuge am PC (`build/medic-cli`):

```bash
medic-cli check   <snapshot-ordner>            # Befunde
medic-cli report  <snapshot-ordner> [--qr]     # Bericht, optional QR-Code im Terminal
medic-cli fix     <snapshot-ordner> <regel-id> # Plan ansehen (mit --apply --yes ausführen, nur an einer Kopie!)
medic-cli history|undo|export ...
```

## Mitmachen

Eine neue Regel braucht: einen Eintrag in `knowledge/rules.json` mit Quelle, ein Positiv- und ein Negativ-Fixture unter `tests/fixtures/` und `expect.json`. Die Tests erzwingen beides. Fixtures sind **synthetisch**: echte Datei- und Spielnamen gehören nicht ins Repo.

## Dokumentation

| Datei | Inhalt |
|---|---|
| `CLAUDE.md` | Einstieg für Claude Code: Ziel, Stack, Grenzen, Arbeitsweise |
| `docs/SPEC.md` | Funktionen, Bedienung, Nicht-Ziele |
| `docs/ARCHITECTURE.md` | Module, Datenfluss, Regel-Format, Sicherheit |
| `docs/RULES.md` | Regelkatalog (22 Regeln) mit Quellen, Status und Abweichungen |
| `docs/ROADMAP.md` | Phasen 0–5 |
| `docs/PHASE2.md`, `PHASE3.md`, `PHASE4.md` | Konsole und Snapshot, Oberfläche und App-Kontext, Bericht und Release |
| `knowledge/` | Regeln (`rules.json`) und JSON-Schema |

## Lizenz und Danksagung

Die Lizenz von PS5 Medic selbst ist **noch nicht festgelegt** (Entscheidung des Autors vor der ersten Veröffentlichung; zu klären ist u. a., wie die GPLv3-Laufzeitbibliotheken des ps5-payload-sdk den fertigen ELF betreffen). Verwendete Fremdkomponenten und ihre Lizenzen: [THIRD_PARTY.md](THIRD_PARTY.md).

PS5 Medic ist ein Community-Projekt und nicht mit Sony verbunden. Die Nutzung geschieht auf eigene Verantwortung.
