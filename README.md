# PS5 Medic

*[English version](README.en.md)*

**PS5 Medic** prüft auf einer gejailbreakten PS5 den **Live-Zustand** des Homebrew-Setups (Firmware, Jailbreak-Kette, kstuff, ShadowMount+, Payloads, Dateien, Konfiguration), erklärt bekannte Fehlerursachen **in Klartext** und repariert – wo das sicher möglich ist – **mit Backup und Rückgängig-Funktion**.

Läuft als Vollbild-App direkt auf der Konsole und wird mit dem DualSense bedient. Ein Knopfdruck: *Check-up starten*.

> Stand: Version 0.4.0-dev. Getestet auf einer PS5 Pro (FW 12.00) mit ShadowMount+ 1.7beta12. Andere Firmwares und Setups sind nicht getestet; die Regeln sind deshalb versionsgebunden formuliert und einzeln als *bestätigt* oder *noch nicht bestätigt* gekennzeichnet.

## Was es kann

- **Check-up**: 26 Regeln (Jailbreak-Kette und HEN, ShadowMount+, Plugins und Speicher), Ergebnis als Liste mit Schweregrad, Klartext, Evidenz (Datei/Zeile/Wert) und Quelle. Der Katalog steht in [docs/RULES.md](docs/RULES.md).
- **Reparieren** (5 Fixes): Image umbenennen (Klammern), ShadowMount+-Einstellung setzen, BackPork aus der Autoload-Liste nehmen, doppelte Kopie derselben TITLE_ID verschieben, Anleitung bei verschachtelten Images. Vorher zeigt die Vorschau genau, was geändert wird.
- **Rückgängig**: Jede Reparatur steht im Verlauf und lässt sich zurücknehmen.
- **Bericht**: Taste □ in der Befundliste schreibt einen Markdown-Bericht nach `/data/ps5medic/reports/` und zeigt eine Kurzfassung als **QR-Code** (mit dem Handy scannen, Text kopieren, ins Forum oder in ein Issue einfügen). IP- und MAC-Adressen, Benutzer- und Konto-IDs sowie Seriennummern werden entfernt; Datei- und Ordnernamen bleiben drin, bitte vor dem Teilen prüfen. Fehlerartige Logzeilen, die zu keiner Regel passen, stehen als **Unbekannte Fehlerzeilen** im Bericht: Stoff für neue Regeln.
- **Regel-Update**: Einstellungen, Regel-Update. Neue `rules.json` (und `error_codes.json`) samt SHA-256-Prüfsumme per FTP oder USB-Kopie auf die Konsole legen **oder** mit □ von der eigenen Adresse herunterladen (`update_url` in `/data/ps5medic/config.ini`; nur mit gültiger Ed25519-Signatur des Autors). PS5 Medic prüft die Datei und installiert sie nach ✕ (mit Backup und Rückgängig).
- **Fehlercodes**: Start, „Fehlercodes“. Nachschlagewerk zu 36 Meldungen wie CE-108262-9 (Erklärung, Schritte laut Sony, Quelle als Text und QR-Code, Prüfdatum; Codes ohne Sony-Seite sind als Community-Angabe gekennzeichnet; nach einem Check-up zeigt der Eintrag, was Medic an der Konsole sieht). Nur lesen und erklären, es wird nichts verändert. Nennt ein Befund einen Code, springt □ zum Eintrag; △ vermerkt einen Code für den Bericht.
- Deutsch und Englisch (Einstellungen).

## Sicherheit

- **Nie löschen**, nur verschieben oder umbenennen. Dateien, die geändert werden, werden vorher gesichert (`/data/ps5medic/backups/`), jede Änderung steht im Journal (`/data/ps5medic/journal.jsonl`).
- **Kein Schreiben ohne ✕**: Reparatur und Rückgängig passieren erst nach ausdrücklicher Bestätigung auf der Vorschau. Weitere Schreibaktionen: der Bericht (□) und das Regel-Update (✕ auf dem Angebot).
- Schreibzugriffe nur in einer Whitelist (ShadowMount+-Ordner, Autoload-Listen, die eigenen Scanpfade, `/data/ps5medic`). Nie unter `/system`, `/system_ex` oder `/dev`.
- **Das Netz wird nur für den Regel-Download benutzt**, nur auf □ und Bestätigung, nur zur Adresse aus deiner `config.ini`, und es wird nichts gesendet außer einer GET-Anfrage. (Der Test, ob der ELF-Loader auf Port 9021 antwortet, geht nur an `127.0.0.1`.)
- Keine Exploits, keine Firmware-Änderungen, keine Funktionen zum Beschaffen, Entsperren oder Verteilen von Spielen, Keys oder Firmware. PS5 Medic repariert die **Umgebung**, nicht Inhalte.

## Voraussetzungen

- Eine bereits gejailbreakte PS5 mit ELF-Loader (Port 9021) und FTP (hier Port 2121).
- Für den Start mit DualSense braucht die UI einen App-Kontext (sonst öffnet die Konsole den Controller nicht, Hintergrund: [docs/PHASE3.md](docs/PHASE3.md)). Den gibt ihr der **Starter** der Kachel (ein kleiner nativer Titel, der den Launcher an den ELF-Lader sendet); **weder websrv noch ein Homebrew-Lader ist nötig**. Ohne Kachel läuft nur der Diagnose-Payload (`ps5medic.elf`).

## Installation

1. Den Ordner `PS5MEDIC` aus dem Release per FTP nach `/data/homebrew/` kopieren (er enthält `ps5medicui.elf`, `ps5medic-launcher.elf`, `ps5medic.elf`, `rules.json`, `error_codes.json`, `homebrew.js` und das Icon).
2. Die Kachel bekommen, auf einem von zwei Wegen:
   - **ELF senden:** `ps5medic-launcher.elf` an Port 9021 senden (Payload Manager, oder vom PC: `make send-launcher PS5_HOST=<PS5-IP>`). Er legt die Kachel **PS5 Medic** (`FAKE42008`) auf dem Startbildschirm an und startet die UI gleich, wenn keine App läuft.
   - **Ordner-App:** den Ordner `FAKE42007` aus dem Release nach `/data/homebrew/` kopieren; ShadowMount+ legt die Kachel an. Danach startet ein Druck auf die Kachel die UI, ohne dass eine ELF gesendet werden muss.
3. Ein Druck auf die Kachel startet die UI. Sie braucht nur den ELF-Lader (Port 9021) der Konsole; nach einem Neustart bleibt die Kachel.

Der Launcher schließt nur Homebrew-Fake-Apps (darunter den Starter der Kachel) und System-Programme; ein laufendes Spiel schließt er nie. Einzelheiten: [docs/TITLE.md](docs/TITLE.md). Der alte Weg über websrv (`make launch-ui`) bleibt als Ausweichmöglichkeit.

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
make send-launcher PS5_HOST=192.168.1.x # Launcher an Port 9021 senden (Kachel FAKE42008 + UI); make title = Ordner-App FAKE42007; make launch-ui = alter Weg über websrv
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

**Tester gesucht** (andere Firmwares und Setups): [docs/TESTER.md](docs/TESTER.md). Probleme melden mit der Issue-Vorlage, den Bericht (□) einfügen.

Eine neue Regel braucht: einen Eintrag in `knowledge/rules.json` mit Quelle, ein Positiv- und ein Negativ-Fixture unter `tests/fixtures/` und `expect.json`. Die Tests erzwingen beides. Fixtures sind **synthetisch**: echte Datei- und Spielnamen gehören nicht ins Repo.

## Dokumentation

| Datei | Inhalt |
|---|---|
| `CLAUDE.md` | Einstieg für Claude Code: Ziel, Stack, Grenzen, Arbeitsweise |
| `docs/SPEC.md` | Funktionen, Bedienung, Nicht-Ziele |
| `docs/ARCHITECTURE.md` | Module, Datenfluss, Regel-Format, Sicherheit |
| `docs/RULES.md` | Regelkatalog (26 Regeln) mit Quellen, Status und Abweichungen |
| `docs/ROADMAP.md` | Phasen 0–5 |
| `docs/PHASE2.md` bis `PHASE5.md` | Konsole und Snapshot, Oberfläche und App-Kontext, Bericht und Release, Regel-Update und Community |
| `docs/TESTER.md` | Mitmachen als Tester |
| `docs/TITLE.md` | Kachel unter „Medien“ (Starter-Titel) |
| `knowledge/` | Regeln (`rules.json`), Fehlercodes (`error_codes.json`) und JSON-Schemas |

## Lizenz und Danksagung

PS5 Medic steht unter der **GNU General Public License, Version 3 oder (nach Wahl) jeder späteren** (GPL-3.0-or-later). Der Lizenztext liegt in [LICENSE](LICENSE), jede eigene Quelldatei trägt eine SPDX-Zeile. Das gilt auch für die Regeldatenbank `knowledge/rules.json`. Grund: Der fertige ELF linkt gegen die GPLv3-Laufzeitbibliotheken des ps5-payload-sdk. Die Fremdkomponenten (MIT, zlib, BSD-2-Clause, DejaVu) sind mit der GPL-3.0 vereinbar und stehen mit ihren Lizenzen in [THIRD_PARTY.md](THIRD_PARTY.md).

Das Repository ist **privat**, es gibt kein öffentliches Release (Entscheidung des Autors, 8. Nov. 2026). Wer ein Paket aus `make dist` weitergibt, muss nach der GPL auch den Quelltext dazu anbieten.

PS5 Medic ist ein Community-Projekt und nicht mit Sony verbunden. Die Nutzung geschieht auf eigene Verantwortung.
