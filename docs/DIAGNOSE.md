# Diagnose-Katalog und Bauplan (Stand 11. Nov. 2026)

Ziel: PS5 Medic wird zum vollständigen System- und Spiele-Doktor, der Hardware- und Software-Probleme einer gejailbreakten Konsole findet, erklärt und, wo sicher möglich, reversibel behebt. Grundlage ist eine Recherche in zehn Teilthemen (Hardware, Systemsoftware, Profi-Methodik, Reverse-Engineering-Techniken, Konsolen-Dateisystem lesend per FTP, Homebrew-Stack, Console Service Tool, Tool-Katalog, zwei Quellcode-Analysen lizenzierter Tools). Der ausführliche Bericht liegt außerhalb des Repositorys; diese Datei hält fest, was daraus gebaut ist, was als Nächstes kommt und was nie gebaut wird.

## Leitregeln

1. **Erst die beobachtete Datei, dann die dokumentierte API, dann der Community-Bericht.** Alles, was nur auf Namen in Stubs ruht, wird zuerst im Kindprozess an der Test-Konsole belegt.
2. **„Nicht geprüft“ ist ein eigener Zustand und heißt nie „gesund“** (Beispiel: Fakt `integrity_skip` je Datenbank).
3. **Evidenz trennen:** beobachtet an der Konsole, dokumentiert, Community, abgeleitet, unbekannt. Regeln ohne Beleg stehen auf `verified: todo`.
4. **Schwellen leben nur in `rules.json`** und sind vorläufig, bis sie an der Test-Konsole geeicht sind.
5. **Fixtures sind synthetisch** (erfundene Titel-IDs, erfundene Konto-IDs); echte Dateinamen, Titel und Kennungen der Konsole stehen nie im Repository.
6. Die harten Grenzen aus `CLAUDE.md` gelten unverändert; insbesondere nennt kein Text je ein Update der Systemsoftware oder Firmware (`text_policy.cpp`).

## Gebaut (Regeln, Collector, Tests)

| Bereich | Collector | Regeln | Quelle der Idee |
|---|---|---|---|
| Spielstände und Konto | `collect_savedata` (ACCOUNT_ID aus `savedata_prospero_meta`) | `savedata.foreign_account` | Beobachtung an der Test-Konsole |
| Startverlauf | `collect_system_logs` (`vshlog.0.txt`) | `sys.unclean_boot`, `sys.unclean_boot_history` | Erkundung der Konsole (nur lesend) |
| Fehlerverlauf | `collect_system_logs` (`error/history/*.json`) | `errors.recurring_title`, `errors.recurring_system_app` | Erkundung der Konsole |
| Datenbanken, Stufe 1 | `collect_databases` (Kopf, Größe, Journal) | `db.header_invalid`, `db.truncated`, `db.journal_pending` | sqlite.org/fileformat2.html |
| Datenbanken, Stufe 2 | `sqlite_check.cpp` (`integrity_check` auf einer Kopie im Speicher, WAL eingerechnet) | `db.integrity_failed` | Idee nach PS5 Cleanup (GPL-3.0-or-later), eigener Code |
| Fehlercodes | Katalog `error_codes.json` (40 Codes) | – | Sony-Seiten, update-freier Wortlaut |
| Abdeckung | `evaluate_coverage` (Liste `coverage` in `rules.json`, 11 Bereiche) | – (Bericht, UI, CLI) | Leitregel „nicht geprüft ≠ gesund“ |
| Verlässlichkeit | `confidence` je Regel (Stufe, Grundlage, Grund; Standard aus `verified`) | – (UI, Bericht, CLI) | Leitregel „Evidenz trennen“ |
| Spielordner | `collect_game_files` (Dateiname nur in der Groß-/Kleinschreibung falsch) | `game.il2cpp_case` (mit Fix `rename_case_prx`), `game.il2cpp_case_latent` | ShadowMount+ #159, #173 (Originale gelesen) |

Details und Befunde: `docs/RULES.md` (Nachträge vom 10. und 11. Nov.). Auf der Test-Konsole sind alle neuen Quellen aus dem Payload ohne Rechteanhebung lesbar (geprüft).

## Bauplan (Reihenfolge)

Sicherheitsleiter: Dateien lesen, bekannte SDK-Sensoren lesen, Unbekanntes im Kindprozess erkunden, Last, Schreiben im eigenen Ordner, Kopieren fremder Dateien in den eigenen Ordner, Config-Änderungen mit Backup.

| Batch | Inhalt | Stand |
|---|---|---|
| 1 Fundament | **gebaut:** Collector-Abdeckung („nicht geprüft“, siehe `docs/RULES.md`); Fix-Pipeline mit Vorschau (Reparatur-Vorschau mit Diff, Bestätigung, Journal, Rückgängig) und **erneuter Prüfung nach der Reparatur** (die UI sagt, ob die Regel noch gemeldet wird); **Konfidenz mit Grund** (`confidence`: Stufe, Grundlage, Grund je Regel; siehe `docs/RULES.md`); **Regelpaket-Governance** (Formatstufe `requires_level`, Rollback-Schutz bestand; Ablaufdatum bewusst nicht); **Befund-Datensatz** mit `status`, `first_seen`, `seen_runs` und Dringlichkeit (über den Verlauf, siehe Batch 4) | **Batch 1 gebaut** |
| 2 Beobachtete Quellen | Startverlauf, Fehlerverlauf, Datenbanken, Fehlercodes ergänzen; offen: Inventar (Downloads, doppelte Pakete, Waisen), Verknüpfungen, Log-Größen | teilweise gebaut |
| 3 Jailbreak-Stack | Regeln mit Maintainer-Deckung, reversible Fixes: **gebaut** `game.il2cpp_case` (+ `_latent`); „Autoload nach kstuff“ im Original nicht belegt (siehe `docs/RULES.md`); offen: Laufwerk abziehen (nur Hinweis), verwaiste Kachel, Payload-Manager-Layout | teilweise gebaut |
| 4 Verlauf | **gebaut:** `history.jsonl` (Regel-IDs je Check-up), Vergleich mit der letzten Prüfung, „behoben seit“, Dringlichkeit; offen: Baselines für Messwerte, Vorher/Nachher-Diff der Fakten, `pending` → `confirmed` | teilweise gebaut |
| 5 Sensorik und Thermik | Proof of Concept im Kindprozess an der Test-Konsole, danach relative Regeln | offen |
| 6 Datei- und Image-Plausibilität | Kopfparser SELF/ELF, exFAT, PKG, TRP, PSF; „nicht prüfbar“ statt Befund bei verschlüsselten Segmenten | offen |
| 7 Selbsttest-Framework | Registry, Statusmodell, `test.*`-Fakten, Marker `selftest.running`; nur lesende Tests | offen |
| 8 Aktive Tests | Wärme-/Lasttest, Laufwerkstest, Schreibtest (eine wiederverwendete Datei, nie löschen), Controller-, Anzeige-, Audio-Test | offen |
| 9 Nach Beweis oder Entscheidung | Titelabgleich in `app.db`, NVMe-Gesundheit, klog-Zusammenfassung, LAN-Tests, UART-Log-Import und NOR-Info im PC-Begleiter | offen |

## Entscheidungen (vorläufig gesetzt, Marc kann jede ändern)

Gesetzt sind die konservativen Empfehlungen aus dem Bericht; sie ändern am heutigen Verhalten nichts.

| Nr. | Thema | Stand |
|---|---|---|
| D1 | Internet-Netztests | **nein**, nur lokale Messungen (die Regel „kein Netz nach außen“ bleibt); Gateway oder LAN-PC erst nach ausdrücklicher Freigabe |
| D2 | Safe-Mode-Hinweise | „Reset/Neuinstallation“ bleibt ausgeschlossen; „Datenbank neu erstellen“ nur mit Vorbehalt (Wirkung auf per ShadowMount+ eingebundene Titel ungeprüft) und mit „Spielstände vorher sichern“, so in den `db.*`-Texten |
| D3 | `kernel_dynlib_resolve` als Import-Sonde | **kein Produktfeature**; Rechteanhebung nur im Kachel-Starter, nie in Sensoren oder Prüfungen |
| D4–D6 | UART-Fehlercodes, NOR-Zusammenfassung, Console Service Tool | nur später im PC-Begleiter (`medic-cli`), nicht in der Konsolen-UI; vom Console Service Tool nur Ideen und nicht schutzfähige Fakten (keine Lizenz), Danksagung ja |
| D7 | Kalibrierung der Schwellen | Messreihe an der Test-Konsole vor jeder Regel über `info`; `sys.unclean_boot` (3 in 14 Tagen) ist vorläufig |
| D8 | Schreibtest | **Marc: eine Datei im eigenen Ordner überschreiben, nie löschen**; 256 MiB, 1 GiB je Lauf, ein Lauf pro Tag sind Vorschläge |
| D9 | Gründlich-Stufe | erst nach Batch 7, mit ausdrücklicher Bestätigung |
| D10 | `/dev/klog` selbst lesen | nein, bis ein Test an der Konsole vorliegt |
| D11 | Fremdwerkzeug-Daten (Cooling Center, Save-Tools) | eigene Historie zuerst, Import optional |
| D12 | Fall-Export | nur lokal (USB/QR), maskiert (Konto-ID, Seriennummer, MAC, IP, Titelnamen, SMB-Passwörter), nie Netz |
| D13 | Gesamt-Health-Score | **nein**; Ampeln je Domäne mit „Datenlage unzureichend“ |
| D14 | Anzeige- und Audio-Tests | nur, wenn die UI Ton und 1:1-Bild nachweislich ausgibt |
| D15 | Konten- und Aktivierungswerkzeuge | erkennen, warnen, Spielstände sichern; nichts aktivieren (Regel `savedata.foreign_account` ist der erste Schritt) |
| D16 | SQLite im Payload | **eingebunden** (gemeinfrei, ohne Dateizugriff übersetzt, nur Kopie im Speicher) |
| D17 | Inhalte sensibler Dateien | nie lesen (Nachrichten, Konten, Spielstandinhalte, Trophäen); Datenbanken nur Kopf und Integrität auf einer Kopie |
| D18 | Router-Hinweise | Router-Neustart ja, Router-Firmware nie |
| D19 | Fehlercodes aus dem Verlauf | **4 von 6 aufgenommen** (`WV-109145-0`, `NW-102261-2`, `WV-109168-5`, `CE-100008-9`); `CE-105773-3` und `CE-118002-2` haben keine Sony-Seite (HTTP 404) und bleiben ohne Beleg offen |
| D20 | Regelpaket-Governance (zwei Schlüssel, Rollback-Schutz) | Rollback-Schutz und Formatstufe **gebaut**; zweiter Schlüssel erst bei einem Schlüsselwechsel; Ablaufdatum nein |
| D21 | Aufräumen der eigenen Altlasten (`probe.txt`, `padprobe.*`, `ui.stdout`) | nur nach `_to_delete/` verschieben, nie löschen |

## Lizenzen und Danksagungen

- **Übernommen als Idee oder Code:** SQLite (gemeinfrei, `third_party/sqlite3/`); Idee der Datenbank-Prüfung nach PS5 Cleanup (da_Chris86, GPL-3.0-or-later). Eintrag in `THIRD_PARTY.md`.
- **Nur Ideen, kein Code und keine Daten:** Console Service Tool (amoamare, keine Lizenz). Anregung für eine spätere Hardware-Fehlercode-Seite im PC-Begleiter; Daten müssten aus Primärquellen stammen.
- **Als Code übernehmbar (Lizenz geprüft, noch nicht eingebaut):** `driftcheck5` (GPL-3.0-or-later, Drifttest), Marcs eigenes Cooling Center (GPL-3.0, README nennt „oder später“; Dateiköpfe fehlen). Quellen mit nur dem GPLv3-Text ohne Versionsaussage werden als „nur Version 3“ behandelt: dort wird nur die Idee neu umgesetzt.
- Alle Lizenzaussagen sind Prüfungen am Repository, keine Rechtsberatung.

## Ausgeschlossen (wird nie gebaut)

- Jeder Rat zu Updates der Systemsoftware oder Firmware (auch von Controller, Fernseher, Router), „Reset/Neuinstallation“ als Schritt; Sony-Fehlercodes mit solchen Schritten werden nur mit update-freiem Wortlaut aufgenommen (Prüfung in `text_policy.cpp` und `test_error_codes.cpp`).
- Exploits, Kernel-Schreibzugriffe, Rechteanhebung außerhalb des Kachel-Starters, Entschlüsseln und Signieren, Backports erzeugen, Spiele oder Schlüssel beschaffen oder verteilen.
- Flash- und NOR-Schreibzugriffe, Schreiben von Lüfter- oder ICC-Werten, `errlog clear`, rohe UART-Befehle, Live-UART in der Konsolen-App.
- Löschen von Nutzerdaten (nur Verschieben nach `_to_delete/` mit Bestätigung, Backup und Undo); Lesen von Nachrichten, Konten, Trophäen- und Spielstandinhalten.
- Netz nach außen ohne ausdrückliche Freigabe (D1).
