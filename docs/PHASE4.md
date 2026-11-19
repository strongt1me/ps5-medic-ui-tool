# Phase 4 – Bericht und Release

**Deutsch** · [English](en/PHASE4.md)

Stand 8. Nov. 2026. Bericht, Anonymisierung, QR-Code, □-Taste, READMEs und `make dist` stehen und sind getestet. **Auf der Konsole noch nicht bestätigt:** ob der QR-Code vom Fernseher mit einem Handy lesbar ist. **Entschieden (8. Nov. 2026):** Lizenz GPL-3.0-or-later; das Repository bleibt privat, kein öffentliches Release.

## Bericht

`libmedic/src/report.cpp`, plattformunabhängig (läuft auch am PC: `medic-cli report <snapshot> [--lang de|en] [--out datei] [--qr]`).

- **Voller Bericht** (Markdown): Kopf mit Zeit, Medic-Version und Regelbasis · Umgebung (Firmware, Modell, Versionen von kstuff/ShadowMount+/etaHEN/ps5-theme soweit bekannt, laufende Payloads aus einer festen Liste bekannter Namen, Speicher je Laufwerk) · Befunde mit Schweregrad, Klartext, Evidenz, Reparatur-Kennung und Quelle samt Status · Reparaturverlauf aus dem Journal · Hinweis zur Anonymisierung.
- **Evidenz mit Faktname** (`Evidenz (free_pct): 1`): `Finding::evidence_path` kommt aus der Regel; ohne ihn wären Werte wie „1" oder „30" nicht zu deuten.
- **Kurzfassung für den QR-Code**: reiner ASCII-Text, höchstens **640 Byte** (`kQrMaxBytes`). Mit Fehlerkorrektur M ergibt das höchstens QR-Version 20 (97 Module, auf einem 1080p-Fernseher 7 px je Modul) und bleibt vom Sofa aus lesbar. Zu lange Berichte werden an Befund-Grenzen gekürzt („… weitere Befunde im Bericht"); der Pfad der Datei steht immer am Ende.
- **Ablage**: `/data/ps5medic/reports/report_<Zeitstempel>.md`. Der Pfad entsteht nur aus einem gültigen Zeitstempel (`report_path_for`), sonst wird nichts geschrieben.

## Schreibregel

Die einzige Schreibaktion außer Reparatur und Rückgängig ist □ in der Befundliste (`IMedicService::make_report`). Tests stellen sicher: Quadrat in den Details oder im Bericht schreibt nichts; mit dem echten `LocalService` kommt genau eine Datei unter `/data/ps5medic/reports/` hinzu; ohne Schreibzugriff (PC-Vorschau) und mit ungültigem Zeitstempel bleibt der Dateibaum unverändert, den QR-Text gibt es trotzdem.

## Anonymisierung

`anonymize()` ersetzt in **allem**, was der Bericht ausgibt (Zeichen-Scan, kein `std::regex`):

| Muster | Ersatz |
|---|---|
| IPv4 (Oktette 0–255; Versionen wie `1.10.2` oder `v1.2.3.4` bleiben) | `[IP]` |
| IPv6 (mit `::` oder acht Gruppen; Uhrzeiten und C++-Namen bleiben) | `[IPV6]` |
| MAC (6 × 2 Hex, `:` oder `-`) | `[MAC]` |
| `/user/home/<8 Hex>` | `/user/home/[ID]` |
| genau 16 Hex-Ziffern (mit `0x` oder mindestens einem Buchstaben a–f; Zeitstempel bleiben) | `[ACCOUNT-ID]` |
| `serial: …`, `seriennummer = …` | `[SERIAL]` |
| `account id: …`, `user_id=…`, `psn id: …` | `[ID]` |

Grenzen, bewusst: **Datei- und Ordnernamen bleiben erhalten**, sie sind meist die Evidenz; der Bericht sagt das und bittet, vor dem Teilen zu prüfen. Seriennummern sammelt kein Collector; maskiert wird nur, was in Evidenz oder Logzeilen als „Schlüssel: Wert" auftaucht. Die Platzhalter sind eckig geklammert, weil spitze Klammern in Markdown als HTML verschwinden.

## QR-Code

Encoder: [Nayukis QR-Code-Generator](https://www.nayuki.io/page/qr-code-generator-library) (MIT) in `third_party/qrcodegen/`, unverändert, Commit in `COMMIT.txt`. Dünner Umschlag in `libmedic/src/qr.cpp`. Die UI kodiert einmal beim Öffnen (nicht in jedem Bild) und zeichnet schwarz auf weiß mit Ruhezone von 4 Modulen.

**Unabhängig geprüft:** Die gerenderten PNGs der UI (`make preview`) wurden mit OpenCV (`QRCodeDetector`) dekodiert und ergaben genau die Kurzfassung (151 und 265 Zeichen). Tests prüfen zusätzlich Suchmuster, Taktmuster, festes dunkles Modul und das Verhalten bei zu langem Text.

## Erster Berichtslauf auf der Konsole (8. Nov. 2026)

□ in der Befundliste, ◯ zurück: `ui.log` zeigt „Bildschirm: Bericht", die Datei `report_<Zeit>.md` lag unter `/data/ps5medic/reports/`, der QR-Code wurde mit einem Handy gescannt. Dabei fielen am echten Bericht zwei Mängel auf, beide behoben und getestet:
- **Leere Mountpunkte** (`/mnt/usb1` … `/mnt/ext0` ohne Laufwerk) meldeten die Größe eines winzigen Dateisystems („0,0 GB, 80 %") und füllten den Speicher-Abschnitt. Der Bericht listet nur Laufwerke ab 1 GB.
- **ShadowMount+ „Version unbekannt"**, obwohl bekannt: Das `debug.log` war nach Stunden länger als die gelesenen letzten 2000 Zeilen, die Startzeile mit der Version steht nur am Anfang. Der Collector prüft jetzt zusätzlich die ersten 16 KiB (Fixture `b3_pos_longlog`). Grenze: Wurde SM+ ohne Log-Rotation neu gestartet, kann die Version vom Log-Anfang älter sein als die laufende.

## Release

- `make dist` baut `dist/ps5-medic-<Version>/` (Ordner `PS5MEDIC` für `/data/homebrew/`, READMEs, `THIRD_PARTY.md`, `SHA256SUMS.txt`) und ein Zip. Nur lokal, `dist/` ist in `.gitignore`.
- README auf Deutsch und Englisch.
- Entfallen: der „PPSA-Ordner mit `param.json`" aus dem ursprünglichen Plan; der Launcher-Eintrag (`ps5/homebrew/`) ersetzt ihn.

**Entscheidungen des Autors (8. Nov. 2026):**
1. **Lizenz:** GPL-3.0-or-later (`LICENSE`, SPDX-Zeile in jeder eigenen Quelldatei). Der ELF linkt gegen die GPLv3-Laufzeitbibliotheken des ps5-payload-sdk; die Fremdkomponenten (MIT, zlib, BSD-2, DejaVu) sind damit vereinbar (`THIRD_PARTY.md`).
2. **Öffentliches Release (geändert am 16. Nov. 2026):** Zuerst blieb das Repository `strongt1me/ps5-medic-ui-tool` privat (8. Nov., privates Pre-Release `v0.4.0-dev.2` am 13. Nov.). Marc hat am 16. Nov. 2026 entschieden, es öffentlich zu stellen und `v0.4.0` als Latest-Release zu veröffentlichen. `make dist` baut das Paket; wer es weitergibt, muss nach der GPL den Quelltext anbieten.
3. Die Lizenztexte der Fremdkomponenten im Paket: `THIRD_PARTY.md` nennt sie mit Lizenz; `make dist` legt `LICENSE` bei.
