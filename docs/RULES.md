# PS5 Medic – Startkatalog der Regeln

Recherche-Stand: 7./8. Nov. 2026. Jede Regel hat eine Quelle. `verified`:
- **doc** = steht in der offiziellen Projekt-Doku/Release-Notes
- **community** = Lösung aus Issue/Forum, von Nutzern bestätigt, nicht vom Entwickler
- **todo** = muss vor Aufnahme selbst geprüft werden

Regeln immer **versionsgebunden** formulieren – die Szene ändert sich wöchentlich.

## A. Jailbreak-Kette & HEN

| ID | Erkennung | Klartext / Empfehlung | Fix | Quelle | Status |
|---|---|---|---|---|---|
| A1 `hen.etahen_with_kstufflite_latest_fw` | etaHEN **und** kstuff-lite laufen gleichzeitig auf einer FW, für die nur kstuff-lite vorgesehen ist | Auf der neuesten JB-Firmware kein etaHEN, sondern kstuff-lite nutzen. Reihenfolge: Jailbreak → kstuff-lite → ShadowMount+. | Hinweis | [kstuff-lite #94](https://github.com/EchoStretch/kstuff-lite/issues/94) | community |
| A2 `hen.jb_too_early` | Erster Prozess der Jailbreak-Kette (elfldr, etaHEN, kstuff, ShadowMount+) startet < 60 s nach dem Konsolenstart (Prozess-Startzeit minus `kern.boottime`) | Nach dem Einschalten ca. 1 Minute warten, bevor der Jailbreak läuft. Nur ein Rat, keine bestätigte Ursache. | Hinweis | [kstuff-lite #94](https://github.com/EchoStretch/kstuff-lite/issues/94) | community |
| A3 `hen.etahen_double_load` | zwei etaHEN-Prozesse | Keine zweite etaHEN-Kopie über eine laufende laden; vorher neu starten. | Hinweis | [etahen-13.60 Releases](https://github.com/GronedWaffel/etahen-13.60/releases) | doc |
| A4 `payload.port_9021_closed` | Verbindung zu 127.0.0.1:9021 wird abgelehnt **und** weder elfldr noch etaHEN läuft | Kein ELF-Loader aktiv → Payloads können nicht gesendet werden. Abhängig vom Autoloader. | Hinweis | [elfldr](https://github.com/ps5-payload-dev/elfldr) (Port 9021) | todo |

## B. ShadowMount+ (SM+)

| ID | Erkennung | Klartext / Empfehlung | Fix | Quelle | Status |
|---|---|---|---|---|---|
| B1 `smp.kstuff_min_version` | kstuff-lite < 1.07 | SM+ benötigt kstuff-lite ab v1.07. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B2 `smp.parentheses_in_pfs_name` | `.ffpfs`/`.ffpfsc` mit `(` oder `)` im Namen | Klammern im Dateinamen führen beim Scan zur Kernel-Panic; umbenennen behebt es. | `rename_image_parentheses` | [SM+ #66](https://github.com/drakmor/ShadowMountPlus/issues/66) | community |
| B3 `smp.autotoggle_crash_1360` | FW 13.60 + SM+ 1.7beta3 + kstuff-lite 1.11 + `kstuff_game_auto_toggle=1` + wiederkehrende Spielabstürze | Workaround: `kstuff_game_auto_toggle=0` (kstuff bleibt geladen). Erst config.ini sichern. | `smp_set_key` | [SM+ #130](https://github.com/drakmor/ShadowMountPlus/issues/130) | community |
| B4 `smp.fw900_kstuff_fork` | FW 9.00 + Dumps starten nicht („can't launch the game or app“) + kstuff-lite von EchoStretch | Nutzer berichten: Drakmors kstuff-Fork statt EchoStretch-Version löst es. | Hinweis | [SM+ #89](https://github.com/drakmor/ShadowMountPlus/issues/89) | community |
| B5 `smp.backpork_conflict` | BackPork-Prozess läuft **und** `backport_fakelib=1` (Standard) | BackPork und SM+-Fakelib vertragen sich nicht – BackPork deaktivieren. | `smp_disable_conflicting_backpork` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B6 `smp.image_extra_folder` | Image enthält `/<ORDNER>/sce_sys/param.json` statt `/sce_sys/param.json` | Spieldateien müssen im Image-Root liegen, ohne zusätzlichen Ordner. Image neu bauen. | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | todo (Erkennung) |
| B7 `smp.ffpfsc_direct_files` | `.ffpfsc` enthält direkt Spieldateien statt verschachteltem Image | `.ffpfsc` ist nur ein Container für ein inneres Image (`.exfat`/`pfs_image.dat`). | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | todo (Erkennung) |
| B8 `smp.duplicate_titleid` | gleiche TITLE_ID aus mehreren Quellen | Pro TITLE_ID nur eine Quelle behalten. | `move_duplicate_titleid` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B9 `smp.custom_scanpath_hides_defaults` | mind. eine `scanpath=`-Zeile **und** Spiele liegen in Standardpfaden | Sobald eigene `scanpath=` gesetzt sind, werden nur noch diese gescannt. | `smp_set_key` (Pfad ergänzen) | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B10 `smp.scan_depth` | Spiele liegen 2 Ebenen tief, `scan_depth=1` | `scan_depth=2` setzen oder Ordner flacher anlegen. | `smp_set_key` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B11 `smp.source_not_stable` | `debug.log` enthält `source not stable yet` wiederholt **für dieselbe Quelle** (mindestens 3 Zeilen je Quelle) | Kopiervorgang noch nicht fertig oder `stability_wait_seconds` zu hoch/niedrig. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B12 `smp.exfat_cluster` | `.exfat`-Image mit Cluster < 64 KB | Langsamer; Images mit 64-KB-Clustern bauen. | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B13 `smp.debug_off` | `debug=0` | Ohne Debug-Log keine Diagnose möglich – für die Fehlersuche `debug=1`. | `smp_set_key` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B14 `smp.register_failed` | `debug.log`: `Register failed` **oder** `[REG] FAIL` | Launcher-Icon entfernen/neu registrieren; als letzter Schritt Spieldaten löschen (Achtung: Saves weg). Der Fehlercode steht in der `[REG] FAIL`-Zeile. | Anleitung | [Issue 141](https://github.com/drakmor/ShadowMountPlus/issues/141) | community (Zeilenformat an einem echten Log belegt, siehe Nachtrag) |
| B16 `smp.shellcore_hook_lost` | `debug.log`: `AppInstallAll hook is no longer installed` oder `AppInstallTitleDir bridge unavailable` | Der Registrierungs-Haken in ShellCore ging verloren (nach Ruhemodus/Spielwechsel): neue Spiele lassen sich nicht registrieren. Payload erneut senden; ab 1.7beta5 stellt SM+ den Haken selbst wieder her. | Hinweis | [Issue 145](https://github.com/drakmor/ShadowMountPlus/issues/145), [Issue 147](https://github.com/drakmor/ShadowMountPlus/issues/147) | community (Zeilen aus echten Logs der Issues) |
| B17 `smp.shellcore_hooks_failed` | `debug.log`: `Failed to Install ShellCore Hooks` | SM+ konnte beim Start keine Haken in ShellCore installieren; Ursache unbestätigt. Sauberer Neustart in der Reihenfolge Jailbreak, kstuff-lite, SM+; neuere Version (ab 1.7beta6 verbesserte Haken). | Hinweis | [Issue 109](https://github.com/drakmor/ShadowMountPlus/issues/109) | community (Meldungstext aus dem Issue; Zeilenformat im Log ungeprüft) |
| B18 `smp.image_cache_full` | `debug.log`: `image cache full` | SM+ nahm einen Mount zurück, weil der Image-Cache voll war (gemeldet bei 64 Einträgen, über 100 Images). Weniger Quellen/Duplikate, Cache nicht von Hand löschen. | Hinweis | [Issue 32](https://github.com/drakmor/ShadowMountPlus/issues/32) | community (Zeile aus dem echten Log des Issues; der Fall war 1.6test11, ob 1.7 noch ein Limit hat, ist offen) |
| B19 `smp.api_not_responding` | `[API] HTTP/JSON ready: http://<0.0.0.0\|127.0.0.1>:<Port>/` im Log, SM+ läuft, Port nimmt auf der Konsole keine Verbindung an (Info) | Der API-Listener kann still ausfallen, während SM+ weiterläuft; Programme, die die API nutzen, erreichen SM+ dann nicht. Payload erneut senden (Schlussfolgerung, nicht bestätigt). | Hinweis | [Issue 152](https://github.com/drakmor/ShadowMountPlus/issues/152) | community (Bereit-Zeile und offener Port an der Test-Konsole bestätigt; der Ausfall selbst nicht nachgestellt) |
| B15 `smp.image_sector_autotune` | `autotune.ini` enthält neuen `image_sector`-Eintrag | SM+ hat die Sektorgrösse angepasst – Spiel einfach erneut starten. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | todo (Erkennung) |

## C. Plugins & Speicher

| ID | Erkennung | Klartext / Empfehlung | Fix | Quelle | Status |
|---|---|---|---|---|---|
| C1 `plugin.ps5theme_old_overlay` | ps5-theme-Version **zwischen 1.30 und 1.53** (Testbuilds; gelesen aus `/data/ps5-theme/logs/log.txt`, sonst aus `/data/etaHEN/plugins/ps5-theme.plugin`) | Frühe Testbuilds konnten ein Overlay zurücklassen, das jeden App-Start verhinderte (CE-105733-9); auf 1.53 oder neuer aktualisieren. | Hinweis | [ps5-theme 1.53](https://github.com/Blaze414/ps5-theme/releases/tag/v1.53) | todo |
| C2 `storage.oversized_reservation` | `/user/app/<ID>/sce_sys/param.json` mit `downloadDataSize` ≥ 4096 MiB, aber Dateien der App zusammen < 1 GiB (nur live auswertbar) | Überdimensionierte Speicherreservierung; App über die PS5-Speichereinstellungen entfernen und neu installieren. | Hinweis | [CD Player 1.1.2](https://prosperostore.net/app/PPSA98427/) | todo |
| C3 `storage.low_space_data` | `/data` < 5 % frei | Wenig Platz: Mounts, Logs und Installationen können scheitern. | Hinweis | eigene Heuristik | todo |

## Erweiterung
1. Wissensbasis des Windows-Doctors (MIT) sichten: [knowledge_base.json](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/knowledge_base.json) und [FONTES_PT.md](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/FONTES_PT.md). Passende Regeln übernehmen (Lizenzhinweis in `THIRD_PARTY.md`).
2. Issues regelmässig auswerten: [SM+ Issues](https://github.com/drakmor/ShadowMountPlus/issues), [kstuff-lite Issues](https://github.com/EchoStretch/kstuff-lite/issues), GBAtemp-PS5-Forum.
3. Eigene Fälle von der FW-4.03-Konsole als Fixtures sichern.

## Umsetzungsstand und Abweichungen (Phase 0, Stand 8. Nov. 2026)
Umgesetzt in `knowledge/rules.json` mit je einem Positiv- und Negativ-Fixture: **A1–A4, B1–B19, C1–C3** (26 Regeln, seit dem 9. Nov. 49, siehe „Nachtrag: Quelltext-Recherche und Sicherheitsprüfung“; B16–B19 aus der Doctor-Wissensbasis, siehe „Nachtrag: Doctor-Wissensbasis“; A2, A4, C1–C3 seit dem 8. Nov., siehe „Nachtrag: A2, A4, C1–C3").

Beim Abgleich mit dem aktuellen SM+-README (drakmor/ShadowMountPlus) gefunden:
- **config.ini hat Sektionen** (`[scan] path=`, `[scan] depth`, `[general] debug`, `[fakelib] enabled`). Der Collector versteht Sektionen und die älteren flachen Schlüssel (`scanpath`, `scan_depth`, `debug`, `backport_fakelib`). Defaults laut README: Tiefe 1, `debug` an. Ob `[fakelib] enabled` standardmässig an ist (B5), steht nicht im README – Annahme „an“ stammt aus dieser Datei.
- **Standard-Scanpfade** sind breiter als ursprünglich notiert (`…/homebrew` und `…/etaHEN/games` je Laufwerk, plus `/mnt/usb0..7`, `/mnt/ext0..1`).
- **`autotune.ini`**: Laut README wandern alte Autotune-Werte nach `profiles.ini` (`[image:DATEI] sector_size`). B15 bleibt als `todo`, bis das Format auf der Konsole geprüft ist.
- **B14** („Register failed“): im README als Benachrichtigung genannt, nicht als `debug.log`-Zeile → `todo`. *Überholt (8. Nov.): Das Zeilenformat ist an einem echten Log belegt, siehe „Nachtrag: Doctor-Wissensbasis“.*
- **B6/B7**: Images lassen sich ohne Mounten nicht inspizieren. Die Regeln werten den Zustand unter `/mnt/shadowmnt/` aus (Layout vorläufig, in Phase 2 verifizieren) → `todo`.
- **A1**: Die Firmware-Grenze steht nicht fest. Die Quelle (kstuff-lite #94) nennt nur FW 13.60; die Regel gilt daher ab `>=13.60`.
- **B3/B4**: Abstürze bzw. Startfehler sind aus einem Snapshot nicht erkennbar; die Regeln prüfen nur die gemeldete Kombination aus FW/Version/Config. Versionen und Herkunft von kstuff kommen vorläufig aus `components` in `system.json` (Konsolen-Quelle offen).

## Fixes (Phase 1, Stand 8. Nov. 2026)
Umgesetzt und getestet (apply → undo = identischer Dateibaum): `rename_image_parentheses` (B2), `smp_set_key` (B3, B9, B10, B13), `smp_disable_conflicting_backpork` (B5), `move_duplicate_titleid` (B8), `fix_image_nesting_hint` (B6, B7; nur Hinweis).
Parameter stehen als `fix_params` in der Regel (siehe `libmedic/include/medic/fixes.hpp`).

Offene Punkte, vor dem Einsatz auf der Konsole zu klären:
- **B5**: Wo BackPork gestartet wird, ist nicht dokumentiert. Der Fix kommentiert den Eintrag in `/data/ps5_autoloader/autoload.txt` aus (Pfad aus einer Dateiliste, Syntax ungeprüft). Gibt es die Datei nicht, ändert der Fix nichts und sagt das. Der laufende BackPork-Prozess endet erst mit dem Neustart.
- **B8**: Doppelte Quellen gehen nach `<Scanpfad>/_duplicates/` (gleiches Laufwerk, kein Kopieren). Bei `scan_depth >= 2` könnte SM+ den Ordner mitscannen – der Plan weist darauf hin. Ob SM+ Ordner mit `_` überspringt, ist ungeprüft. *Überholt (9. Nov.): SM+ überspringt nur Namen mit `.` am Anfang; der Fix verschiebt jetzt nach `<Scanpfad>/.duplicates/`.*
- **B9/B10**: Der Fix schreibt in die Sektion `[scan]` bzw. bleibt beim flachen Format, wenn die Datei keine Sektionen hat.
- **Löschen**: Es wird nichts gelöscht. Einzige Ausnahme: beim Undo werden Ordner entfernt, die der Fix selbst angelegt hat und die leer sind (z. B. `_duplicates/`).

## Nachtrag: Abgleich mit dem lokalen SM+-README (1.7beta5-Stand)
Quelle: `README.md`, `config.ini.example` und der Payload-Manager-Quelltext aus dem lokalen Ordner `PS5 SDK usw`. Daraus folgt:
- **Zwei config.ini-Formate** existieren je nach Version: flache Schlüssel (dieses README) und Sektionen (GitHub-Seite, neuere Entwicklung). Collector und Fix-Engine verstehen beide.
- **B3**: `kstuff_game_auto_toggle` ist standardmässig `1`. Die Regel feuert deshalb auch, wenn der Schlüssel fehlt (Fixture `b3_pos_default`).
- **B10**: `recursive_scan=1` erzwingt `scan_depth=2` (Fixture `b10_neg_recursive`). Der Ordner `backports` zählt nicht als Spielquelle (Fixture `b8_neg_backports`).
- **B6/B7**: Mountpunkte `/mnt/shadowmnt/<image>_<hash>` und `/mnt/shadowmnt/pfsc/<image>_<hash>` sind dokumentiert → Status `doc`.
- **B15**: `image_sector=<datei>:<grösse>` in `autotune.ini` ist dokumentiert (SM+ schreibt es, wenn die Cluster-Grösse nicht zur Sektorgrösse passt) → `doc`.
- **B14** bleibt `todo`: „Register failed“ wird im README als Benachrichtigung genannt. *Überholt (8. Nov.): Das Zeilenformat ist an einem echten Log belegt, siehe „Nachtrag: Doctor-Wissensbasis“.*
- **B5-Fix**: Die Autoload-Listen (`/data/ps5_autoloader/autoload.txt`, Payload Manager: `/data/pldmgr/autoload.txt`) kennen keine Kommentare: `!<ms>` ist eine Wartezeit, jede andere Zeile ein Dateiname. Der Fix **entfernt** den BackPork-Eintrag (Original im Backup, `undo` stellt es wieder her) statt ihn mit `#` zu markieren. Der Pfad der Liste hängt vom Autoloader ab; beide sind in der Whitelist.

## Nachtrag: A2, A4, C1–C3 (8. Nov. 2026)
Die fünf letzten Regeln brauchten neue Fakten. Dafür kennt die Plattform jetzt Prozess-Startzeiten (`ProcInfo`), den Konsolenstart (`boot_epoch`) und einen Port-Test (`local_port_state`); Snapshots und Export tragen sie mit (`procs.json`-Objekte `{"name","start"}`, `system.json` `boot` und `ports`).

- **A2**: Die Quelle ist schwach. Im Issue rät ein Community-Mitglied, „a min or so" zu warten, ohne gemessene Zeit und ohne nachgewiesene Ursache (gleichzeitig wurden DNS und Cookies geändert). Die Regel ist deshalb nur ein Hinweis (`info`, `community`). Das Log von ShadowMount+ hat nur Uhrzeiten (`[15:12:42]`), keine Uptime; darum rechnet die Regel mit Prozess-Startzeiten statt mit Log-Zeitstempeln. Auf der Test-Konsole: Jailbreak-Kette 188 s nach dem Start (kstuff zuerst), die Regel feuert dort nicht.
- **A4**: Die Prüfung ist eine Verbindung auf 127.0.0.1:9021 ohne Daten. Nur „Verbindung abgelehnt" zählt als geschlossen; Zeitüberschreitung (Loader beschäftigt) bleibt unbekannt, dann fehlt der Fakt. Zusätzlich darf weder `elfldr` noch `etaHEN` laufen (etaHEN bringt seinen eigenen Loader mit). Auf der Test-Konsole: Port offen. Status bleibt `todo`; der „geschlossen"-Fall ist nur am PC (Fixtures) getestet.
- **C1**: Die Grenze „älter als 1.23" aus der ersten Recherche ist **nicht belegt**: 1.23 ist die Version, ab der sich das Plugin selbst aktualisiert. Laut den Release-Notes von 1.53 stammt der Fehler CE-105733-9 aus „earlier test builds" des Icon-Features, das mit 1.53 erschien. Veröffentlicht sind 1.30 und 1.53, die Regel meldet daher Versionen **zwischen 1.30 und 1.53** (die davor erschienenen Versionen 1.22 bis 1.30 dürften nicht betroffen sein, weil das Icon-Feature erst mit 1.53 erschien; das ist eine Ableitung). Der Fehlercode selbst ist aus einem Snapshot nicht erkennbar. Die Version kommt aus dem Log (`ps5-theme 1.23 starting`; das Format ist aus den Zeichenketten des Plugins abgeleitet, ein echtes Log lag nicht vor) oder fest eingebaut aus der Plugin-Datei (an einer echten `ps5-theme.plugin` 1.23 geprüft). Auf der Test-Konsole ist ps5-theme nicht installiert. Status `todo`.
- **C2**: Die Quelle (Release-Notes CD Player 1.1.2) nennt nur die Größen (16 GiB statt 256 MiB). Dass die Reservierung in `param.json` als `downloadDataSize` (MiB) steht, ist eine Ableitung: die Werte der Test-Konsole passen (256 bei BFpilot und USB-Formatierer, 64, 0). Ein überdimensionierter Fall war nicht reproduzierbar (CD Player ist nicht installiert). Gemeldet wird nur, wenn die Reservierung ≥ 4096 MiB ist **und** die Dateien der App zusammen unter 1 GiB bleiben; ist der Inhalt größer oder nicht sicher bestimmbar (über 20 000 Einträge), bleibt die Regel still. Der Snapshot-Export lässt `/user/app` weg (er kennt keine Dateigrößen), aus einem Export kann deshalb kein C2-Befund entstehen; ein Test sichert das ab. Status `todo`.
- **C3**: Die Schwelle 5 % ist eine eigene Heuristik ohne Quelle (Verweis auf dieses Repo, weil das Schema eine URL verlangt). Nur `/data` zählt; volle USB-Laufwerke lösen nichts aus (Fixture `c3_neg`).

## Nachtrag: B11 nach Befund auf der Konsole (8. Nov. 2026)
Die Regel zählte `source not stable yet` im ganzen Log (mindestens 3 Zeilen). Auf der Test-Konsole löste sie damit einen Hinweis aus, der keiner war: Jeder normale Kopiervorgang erzeugt ein bis zwei solche Zeilen, bevor ShadowMount+ die Quelle installiert (echtes Log: ein Spiel zwei Zeilen, dann `Installing:`; unser eben hochgeladener Starter eine). Drei Zeilen aus **verschiedenen** Quellen sind Normalbetrieb. Jetzt zählt der Collector je Quelle (`smp.unstable_sources[]`, Schlüssel ist der Pfad in der Zeile) und die Regel feuert ab 3 Zeilen derselben Quelle. Fixtures: `b11_neg_distinct_sources` (die Zeilen des echten Logs) und `b11_pos_named` (Name und Pfad mit Klammern). Die Regelversion ist deshalb `2026.11.09`; **bei jeder inhaltlichen Änderung der Regeln `db_version` hochzählen**, sonst lehnt das Regel-Update die Datei als „nicht neuer" ab. Für den Download danach `make sign-rules` (siehe `docs/PHASE5.md`).

## Nachtrag: Doctor-Wissensbasis gesichtet (8. Nov. 2026)
Gesichtet wurde die `knowledge_base.json` des PS5 Jailbreak Doctors (MIT, Version 2026.10.17.1: 32 Regeln und 5 Einträge einer Kompatibilitätsmatrix). Der Doctor wertet **Logs am PC** per Textmuster aus; PS5 Medic wertet **Fakten** der Konsole aus. Übernommen wurde deshalb, was sich aus dem ShadowMount+-Log oder einem Live-Fakt erkennen lässt, mit eigenen Texten. Die Quellen wurden **einzeln bei GitHub nachgeprüft** (Status, Wortlaut der Logzeilen, Release-Notizen); der Doctor-Stand ist vom 17. Okt. und teils überholt (z. B. sind die Issues 152 und 153 inzwischen geschlossen).

**Übernommen:** B16 (Haken verloren; Issues 145/147), B17 (Haken nicht installiert; Issue 109), B18 (Image-Cache voll; Issue 32), B19 (API antwortet nicht; Issue 152, **mit Live-Port-Prüfung statt Log-Muster**). B14 erkennt jetzt auch `[REG] FAIL: 0x…` (siehe unten).

**Schon abgedeckt:** Klammern im Image-Namen (B2), doppelte TITLE_ID (B8), „source not stable“ (B11), Scanpfad-Probleme (B9/B10), 1.7beta3-Absturz mit Auto-Toggle (B3), Registrierung fehlgeschlagen (B14), BackPork-Konflikt (B5).

**Nicht übernommen, mit Grund:**
- *Brauchen klog oder Absturzprotokolle des Spiels, die PS5 Medic nicht liest:* PRX-Modul-Fehler, `param.json` fehlt/ungültig, `libc.prx` fehlt, „crashed before kstuff pause“, SIGSEGV/SIGBUS, Kernel-Panic nach kstuff-Laden, `/dev/klog` busy, „connection refused“ (das ist eine PC-Meldung des Doctors).
- *Ereignisse statt Zustand, nicht aus einem Snapshot erkennbar:* Kernel-Panic beim Verschieben USB→ext1 (Issue 127), CE-105773-3 nach dem Beenden eines Ordner-Spiels (128), CE-108262-9 (143), Trophäen-Registrierung 0x80551618 (97, die Meldung erscheint am Bildschirm, eine Logzeile ist nicht belegt), FPKG nach Reset nicht erkannt (140/149).
- *Zu allgemein, würde auch gesunde Logs treffen:* „awaiting same-app handoff“ (steht im gesunden Log der Test-Konsole bei jedem Spielende; Issue 153 ist zudem behoben), allgemeine Installationsfehler, „Spiel startet nicht“.
- *Quelle nicht auffindbar:* „`/preinst/common/font`“ (Issue 132 liefert 404; der Zugriff wurde laut Release 1.7beta5 ohnehin freigegeben).
- *Nicht belegt oder nicht prüfbar:* Dateinamenlänge (Grenze unbekannt), Mount mit `Invalid Argument` (0x16; im Issue kein Logwortlaut), 80F50005 als eigene Regel (ein Titel, keine bekannte Ursache; der Code steht jetzt in der Evidenz von B14).
- *Kompatibilitätsmatrix:* Hinweise zu 1.7beta2 + LZ4 + kstuff 1.11 und zu 1.7beta5 („etaHEN nicht mit diesem Build“) beschreiben Einzelfälle oder allgemeine Release-Hinweise; die zweite deckt `hen.etahen_with_kstufflite_latest_fw` im Kern ab.

**B14 und die Sprache:** Die `NOTIFY:`-Texte von SM+ folgen der Konsolensprache (`language=auto`; auf der Test-Konsole stehen sie auf Deutsch, z. B. „Duplikat … ignoriert“). Das Muster `Register failed` träfe auf einer deutschen Konsole daher nie. Die Zeile `[REG] FAIL: 0x…` ist nicht übersetzt; B14 erkennt beides. Zeilenformat nach Issue 141: `[REG] FAIL: 0x80f50005` und `NOTIFY: Register failed: <Name> (<TITLE_ID>)`. Gesunde Zeilen (`[REG] Installed NEW!`, `[REG] Skip (already present in app.db)`, `AppInstallAll bridge consumed: … result=0x00000000`) treffen nicht (Fixture `b16_neg`).

**B19 im Detail:** SM+ schreibt beim Start `[API] HTTP/JSON ready: http://0.0.0.0:10101/api/v1 …` (echtes Log, 1.7beta11, Test-Konsole). Daraus entstehen `smp.api.bind`, `smp.api.port` und, wenn die Plattform den Port prüfen kann, `smp.api.state` (Verbindungsversuch an `127.0.0.1`). Ohne Bereit-Zeile (API aus, ältere Version, Start nicht abgeschlossen) gibt es keine Fakten und keinen Befund; bei Bindung an eine bestimmte LAN-Adresse sagt ein Loopback-Versuch nichts, der Zustand bleibt unbekannt. Auf der Test-Konsole ist der Port offen, es gibt keinen Befund. Die Regelversion ist `2026.11.10`.

## Nachtrag: ShadowMount+ 1.7beta12, Konfigurationsformat v2 (8. Nov. 2026)
Am 8. Nov. erschien 1.7beta12 mit neuem Konfigurationsformat (v2): `config.ini` mit `[config] version = 2` und Sektionen (`[general] debug`, `[scan] depth/path/interval_seconds/stability_wait_seconds`, `[fakelib] enabled`, `[api] bind_address/port`, …), Werte `on/off/true/false/yes/no/1/0`, dazu `profiles.ini` für einzelne Spiele und Images (`[game:TITLE_ID]`, `[image:DATEI] sector_size`). Beim ersten Start wandelt SM+ die alten Dateien um (`config.ini`, `autotune.ini`, `smb.json`; Sicherung `config-v1-backup`), Änderungen an der `config.ini` gelten laut Release-Notiz zur Laufzeit. Die Test-Konsole läuft noch mit 1.7beta11 (flache Schlüssel); *Überholt (9. Nov.): Die Test-Konsole läuft seit dem 9. Nov. mit 1.7beta12, das Ergebnis steht am Ende dieses Abschnitts.*

Was daraus folgt (geprüft an `src/sm_config_file.c` und den Beispieldateien von 1.7beta12):
- Die Collectors lasen schon beide Formate (Sektion mit `legacy_key` als flachem Schlüssel, `on/off` als Wahrheitswerte). Neu: **Werte werden wie bei SM+ gelesen**: `#` und `;` leiten einen Kommentar hinter dem Wert ein (v1 und v2), in v2 darf ein Wert in Anführungszeichen stehen (dann zählen `#`/`;` darin nicht, Backslash maskiert). Vorher wäre `path = "/mnt/usb0/Meine Spiele"` mit den Anführungszeichen als Scanpfad gelesen worden (falsche Befunde bei B9/B10). Fixtures `b13_pos_v2`, `b13_neg_v2_defaults`, `b13_pos_v1_comment` und Fakt-Tests.
- Der INI-Editor (Fixes B3, B9, B10, B13) setzt in einer v2-Datei `depth=2` bzw. `debug=1` ans Ende der passenden Sektion und lässt Kommentare und Reihenfolge stehen; SM+ trimmt Schlüssel und Wert, `1` ist ein gültiger Wahrheitswert. Tests mit einer v2-Datei, deren Standardwerte auskommentiert sind (ersetzt eine vorhandene Zeile samt Kommentar dahinter).
- **B15** (`autotune.ini`, `image_sector`) kann unter v2 nicht mehr feuern: `autotune.ini` ist aufgegeben, automatische Sektorkorrekturen stehen in `profiles.ini` (`[image:DATEI] sector_size`), ohne dass sich eine automatische von einer manuellen unterscheiden ließe. Bleibt `todo`.
- **B3** (`kstuff_game_auto_toggle`) gilt nur für 1.7beta3; in v2 gibt es den Schlüssel nicht mehr (die alte Auto-Pause wurde mit 1.7beta5 entfernt).
- Die Scanpfad-Regeln gelten weiter: „mindestens ein `[scan] path` ersetzt die eingebauten Pfade“ (B9); neu ist, dass `/mnt/shadowmnt` und `/mnt/shadowmnt/pfsc` immer dazukommen, das ändert keinen Befund.
### Auf der Konsole bestätigt (9. Nov. 2026, 1.7beta12, FW 12.00)
Der Check-up lief mit einer von SM+ selbst umgewandelten Datei; dazu der Snapshot am PC (`medic-cli check`) und je eine Kopie mit geändertem Wert.
- **Umwandlung:** SM+ legt beim ersten Start `config-v1-backup/` an (Logzeile `[CFG] configuration migrated to v2; originals saved in /data/shadowmount/config-v1-backup`) und schreibt eine **schlanke** `config.ini` nur mit den zuvor gesetzten Werten (`[config] version = 2`, `[general]`, `[emulators]`, `[fakelib]`, `[library]`, `[api]`, `[fan]`, `[scan]`), ohne die Kommentarvorlage. Die alte Zeile `fakelib_mode=<TITLE_ID>:disabled` steht jetzt in `profiles.ini` als `[game:<TITLE_ID>] fakelib = disabled`. Neue Schlüsselnamen aus der Praxis: `[api] bind_address`, `[scan] depth`, `[fan] target_temperature = system`, `[library] remove_missing` / `remove_delay_seconds`, `[fakelib] default_mode`, `[emulators] update_enabled`. Im Backup liegen die alten `config.ini`, `autotune.ini`, `smb.json` sowie `ready.ini` und `profiles-v2.ini` (Letztere vermutlich Übergabedateien der Umwandlung, nicht weiter geprüft).
- **Logzeilen unverändert:** `[CFG] loaded: … api=0.0.0.0:10101 … scan_depth=2 …` (nur um neue Felder erweitert, `fan_target_temperature=0 (0=system)`) und `[API] HTTP/JSON ready: http://0.0.0.0:10101/api/v1 (v1, workers=8, limit=32)` haben das gleiche Format wie unter beta11. Die Fakten `smp.version` (`1.7beta12`), `smp.api.bind`, `smp.api.port` und `smp.api.state` (`open`) kommen daraus unverändert an; B19 feuert nicht.
- **B10:** mit `[scan] depth = 2` kein Befund (vorher unter beta11 mit der flachen Datei einer); mit `depth = 1` in einer Kopie feuert die Regel, die Fix-Vorschau schreibt `depth=2` in `[scan]`. **B13:** `debug = 0` in `[general]` → Befund, Vorschau schreibt `debug=1` in `[general]`. **B9:** ein `path = "…"  # Kommentar` in `[scan]` → Befund (Anführungszeichen und Kommentar werden wie bei SM+ gelesen). Alle drei wie am PC erwartet.
- **Gesamtbild:** Die Konsole und der PC-Check über denselben Snapshot liefern dieselben vier Befunde (`smp.parentheses_in_pfs_name`, `smp.duplicate_titleid`, A2 `hen.jb_too_early` mit 52 s nach dem Konsolenstart, B12 `smp.exfat_cluster`). B12 trifft genau das eine Image mit 32-KB-Clustern (Startsektor: Sektor-Shift 9 + Cluster-Shift 6); die übrigen Images haben 64 KB und bleiben ohne Befund. A2 und B12 waren beim letzten Lauf unter beta11 noch nicht in den Regeln (damals 17 statt 26).
- **Offen war am 9. Nov. vormittags:** Laufzeit-Reload und das Verhalten gegenüber flachen v1-Zeilen in einer v2-Datei. Beides ist am selben Tag **am Quelltext** geklärt (nicht auf der Konsole ausprobiert), siehe den nächsten Abschnitt.

## Nachtrag: Quelltext-Recherche und Sicherheitsprüfung (9. Nov. 2026)
Vier Auswertungen am 9. Nov.: ShadowMount+ (Quelltext HEAD = 1.7beta12, Issues seit August), der übrige Jailbreak-Stack (Payload Manager 0.6.3, WebKit-Autoloader, elfldr, etaHEN, kstuff-lite, nanoDNS), PS5-System- und Speicherfehler (Sony-Support-Seiten, SDK-Beispiele) und eine Prüfung der Fix-Engine auf Sicherheit und Absturzfestigkeit. Danach hat die Regeldatenbank **49 Regeln** (`db_version` 2026.11.11), 23 davon neu. Jede neue Regel hat Positiv- und Negativ-Fixtures; auf der Test-Konsole (beta12) feuert keine der neuen Regeln fälschlich.

**Antworten auf die offenen Punkte (aus `sm_config_*.c`, `sm_scan_tree.c`, `sm_scanner.c` 1.7beta12):**
- **Flache v1-Zeilen in einer v2-Datei werden nicht gelesen.** Jede Zeile gehört zur zuletzt gesehenen Sektion; SM+ übernimmt nur bekannte Paare (Sektion, Schlüssel) und schreibt für den Rest `[CFG] unknown setting` ins Log. Ein anderes Werkzeug, das unter v1 `scan_depth=2` ans Ende der Datei hängte, bewirkt unter v2 also nichts. Der Collector zählt solche Zeilen deshalb nicht mehr (Tiefe, Scanpfade). Neue Regel `smp.v1_keys_in_v2` samt Fix `smp_translate_v1_keys`; die Entsprechungen sind an der Umwandlung der Test-Konsole belegt (altes `config-v1-backup/config.ini` gegen die neue).
- **Laufzeit-Reload:** SM+ beobachtet `config.ini` und `profiles.ini` per kqueue (250 ms Entprellung). Live gelten API, Scanpfade, Tiefe, Intervall, Sprache, debug und quiet; `profiles.ini` erst beim nächsten Mount, das Lüfterziel beim Spielstart. Logzeilen: `[CFG] loaded: …`, danach `[CFG] runtime config reloaded` oder `… reload failed`. Ein Schlüssel, der einen Neustart verlangt, wurde nicht gefunden.
- **Ein ungültiger Wert verwirft die ganze Konfiguration** (1.7beta12; unter beta11 wurde er nur übersprungen). Beim Start beendet sich der Payload, die Meldung steht nur auf der Konsole; beim Reload bleibt der alte Stand. Neue Regel `smp.config_rejected` prüft die Datei nach SM+-Regeln (Wahrheitswerte, Tiefe 1–2, Intervall 1–3600, Wartezeiten, Lüfterziel `system` oder 50–91, Zeilen ohne `=`, `version` ≠ 2) und wertet `[CFG] invalid/cannot read` im Log aus.
- **Scanner:** Namen mit `.` am Anfang werden auf jeder Ebene übersprungen, `_` nicht. Folge: `_duplicates` wurde bei `scan_depth=2` mitgescannt, die verschobenen Doppelten wären zurückgekommen. **Der Fix `move_duplicate_titleid` verschiebt jetzt nach `<Scanpfad>/.duplicates/`**, und der Collector spiegelt die Regel des Scanners (Punkt-Namen überspringen, `_…` nicht). Das erledigt den offenen Punkt „B8: ob SM+ `_`-Ordner überspringt“.
- **Autotune (B15):** unter v2 schreibt SM+ `[image:<Datei>] sector_size` in die `profiles.ini` und loggt sprachneutral `[CFG] image sector autotuned: <Datei>=<N>`; B15 erkennt jetzt beides.

**Neue Regeln** (Stufe: doc = Quelltext/Doku, community = Issue, todo = eigene Schwelle oder unsichere Erkennbarkeit):
| ID | Erkennung | Stufe |
|---|---|---|
| `smp.config_rejected` (kritisch) | v2-Datei mit Wert, den SM+ ablehnt, oder `[CFG] invalid/cannot read` im Log | doc |
| `smp.v1_keys_in_v2` (+ Fix) | flache v1-Namen in einer v2-Datei | doc |
| `smp.v1_config_restored` (kritisch) | v1-`config.ini`, obwohl `config-v1-backup/ready.ini` existiert: SM+ verweigert den Start | doc |
| `smp.retry_limit` | `[RETRY] limit reached` / `retry limit reached (` | doc |
| `smp.kstuff_wait` | `[KSTUFF] not found; waiting …` ohne `ready; continuing startup` | doc |
| `smp.scanner_root_denied` | `[SCAN] watcher open failed` + `scanner watcher initialization failed` (bis beta11) | community |
| `smp.launch_busy` | mehrfach `launch mount unavailable … status=16` (bis beta11) | community |
| `smp.fakelib_handoff_stuck` | `handoff cleanup failed for pid=` + `sandbox fakelib unavailable` (bis beta11) | community |
| `smp.backport_overlay_denied` | `backport overlay failed: … Operation not permitted` | community |
| `smp.leak_pre_beta11` | SM+ 1.7beta9 oder 1.7beta10 (Speicherleck je Scan) | community |
| `smp.usb_game_closed_on_rest` | `[SLEEP] killing USB-backed game before suspend` | doc |
| `autoload.elfldr_without_delay` (+ Fix) | elfldr, danach sofort der nächste Eintrag ohne `!<ms>` | doc |
| `autoload.entry_missing` | Eintrag ohne Payload (`local/<name>`-Ordner unter `payloads/` bei Payload Manager 0.6, Datei beim Autoloader, auch nur falsche Groß-/Kleinschreibung) | doc |
| `autoload.entry_duplicate` | derselbe Eintrag mehrfach | doc |
| `pldmgr.autoload_syntax` | Liste des Payload Managers ungültig (über 4095 Byte, Zeile über 511 Zeichen, `!` ohne Ziffern, Komma) | doc |
| `pldmgr.autoload_disabled` | `AUTOLOAD_ENABLED=0` bei nicht leerer Liste | doc |
| `payload.elf_invalid` | `.elf` in Payload-Ordnern, Autoloader, `/data/homebrew` oder USB-Wurzel zu klein, ohne ELF-Kennung (z. B. HTML-Fehlerseite) oder abgeschnitten (Programm-Header über das Dateiende, Prüfung wie `elfldr_sanity_check`) | doc |
| `nanodns.log_problem` | im nanoDNS-Log `all upstreams failed` (ab 3), `bind(…) failed`, `warning:`; es wird nur gezählt, Domains gelangen nie in Fakten, Befund oder Export | doc (Zeilenformat aus dem Quelltext, kein echter Fehlerfall gesehen) |
| `hen.etahen_crashed` | `main etaHEN has crashed` im `etaHEN.log` oder `etaHEN_crash.log` | doc |
| `hen.etahen_game_rest_kill` | `Game_rest_kill=1` / `Util_rest_kill=1` in der etaHEN-`config.ini` | doc |
| `storage.usb_mounted_ro` | `/mnt/usbN` oder `/mnt/extN` mit echtem Dateisystem und `MNT_RDONLY` | todo (Ursache unbelegt) |
| `sys.temp_high` | CPU oder SoC über 85 °C (`sceKernelGetCpuTemperature`, `sceKernelGetSocSensorTemperature`; auf der Test-Konsole im Leerlauf 36 und 34 °C gemessen) | todo (Schwelle eigene Annahme; Sony nennt keine Grenze) |
| `storage.scanpath_drive_missing` | eigener Scanpfad auf einem Laufwerk, das nur als tmpfs-Platzhalter (2 MiB) gelistet ist | doc |

**Änderungen an bestehenden Regeln:**
- **B2 `smp.parentheses_in_pfs_name`: von „kritisch“ auf „Hinweis“.** Issue #66 ist geschlossen, der Entwickler schreibt „längst behoben“; gemeldet war 1.6test15 auf FW 6.02, und es ließ sich mit 1.7beta4 auf FW 12.00 nicht nachstellen. Der Fix (Klammern entfernen) bleibt als harmlose Vorsorge.
- **B15** erkennt die v2-Logzeile, **B17** zusätzlich `[SHELLCORE] lifecycle hooks unavailable; stock behavior kept` (sprachneutral; die Notification ist lokalisiert).
- **B16/B18/B19 laut Quelltext 1.7beta12 zu relativieren, Regeln unverändert:** Den Wortlaut `AppInstallAll hook is no longer installed` gibt es in beta12 nicht (steht nur in Logs älterer Versionen); `image cache full` steht jetzt als `[IMG] image cache full, refusing unmanaged mount reuse:` und die Grenze liegt bei 2048 statt 64 Images; der API-Listener heilt sich seit beta6 selbst (`[API] HTTP listener lost … recovery scheduled`).
- **B5-Nachtrag berichtigt:** Die Liste des WebKit-Autoloaders (`ps5_autoloader`) kennt `#`-Kommentare (auch hinter Text), `@sync` und absolute Pfade; „kennt keine Kommentare“ gilt nur für die Payload-Manager-Liste. SM+ beendet BackPork beim Start selbst (`[FAKELIB] stopped conflicting …`, seit mindestens beta8); B5 trifft daher nur noch BackPork, das nach SM+ gestartet wurde.
- **Versionsvergleich:** `compare_versions` kennt jetzt Vorabversionen (`1.7beta9` < `1.7beta10` < `1.7beta11` < `1.7`; test < alpha < beta < rc). Vorher galt `1.7beta3` als gleich `1.7`.

**Geprüft und verworfen (Auswahl):**
- *`pldmgr.layout_downgrade`* (Dateien direkt unter `payloads/` außer `repo/` und `local/`): am echten Layout widerlegt, denn der Payload Manager 0.6.3 legt sich selbst unter `payloads/pldmgr/` ab; die Regel hätte auf der gesunden Konsole gefeuert.
- *`game.requires_newer_fw`* (Spiel braucht neuere Firmware als installiert): berührt die Grenze „nichts zum Entsperren von Spielen“; nur als Hinweis denkbar, **Marc entscheidet**, ob er gewollt ist.
- *`dns.nanodns_blocks_psn`* (Standard-`nanodns.ini` sperrt Sony-Domains): die Sperre ist der Zweck von nanoDNS, ein Dauerhinweis wäre Lärm.
- *Kritische Temperaturgrenze, Lüfter-Regeln, M.2-Spiel-Kill beim Ruhemodus, Fehlercode-Lexikon (CE-/NW-Codes):* Grenze unbelegt bzw. Ereignis statt Zustand bzw. eigene UI-Funktion; das Lexikon ist als Folgeaufgabe vorgemerkt.
- *Dateisystem-/Partitionsregeln für USB (MBR/GPT, Hub):* Sony schweigt dazu, aus einem Snapshot nicht belegt lesbar.

**Privatsphäre:** Der Export schwärzt Werte von Schlüsseln mit `password`, `passwd`, `secret` oder `token` in `config.ini`, `profiles.ini` und `autotune.ini` (SM+ speichert SMB-Passwörter im Klartext, auch in `config-v1-backup/`). `config-v1-backup/` selbst wird nicht exportiert, nur das Vorhandensein von `ready.ini`. Das nanoDNS-Log wird nur gezählt und als Zeilen ohne Domains exportiert; ELF-Dateien erscheinen als 64-Byte-Ersatzdateien mit demselben Prüfergebnis.

## Nachtrag: Sicherheitsprüfung der Fix-Engine (9. Nov. 2026)
Ein Prüfer hat die Fix-Engine und den Regel-Download gelesen (Befunde einzeln gegen den Code geprüft). Keine Umgehung von Bestätigung oder Signatur; Schwächen bei Absturz-/Stromausfall-Konsistenz, Undo und Whitelist, jetzt behoben und getestet (`recovery_tests`, `stack_tests` in `tests/test_fixes.cpp`):
- **Abgebrochene Transaktionen** (Stromausfall, USB abgezogen) ließen sich nicht zurückholen: Jede Operation steht jetzt **vor** der Ausführung als `intent` im Journal, mit Backup-Pfad, Hashes, anzulegenden Ordnern und den erlaubten Wurzeln. `undo_txn` stellt je Operation fest, ob sie im Zustand „nachher“ (zurückdrehen), „vorher“ (schon erledigt) oder fremd ist (abbrechen, nichts anfassen). Es geht deshalb auch für Transaktionen im Zustand `incomplete` und lässt sich nach einem halb gescheiterten Undo wiederholen; die UI bietet Rückgängig dafür an. Ein Rollback, das selbst scheitert, ergibt `incomplete` statt `aborted`.
- **fsync und atomares Schreiben:** `PosixRw` schreibt über eine Temp-Datei (`O_EXCL|O_NOFOLLOW`, vorher entfernt), `fsync` für Datei und Ordner, dann Umbenennen. Das **Journal wird nur angehängt** (`append_file`, `O_APPEND`, `fsync`) statt jedes Mal komplett neu geschrieben. Unlesbare Journalzeilen (Rest eines abgebrochenen Schreibens) werden übersprungen; der Commit-Eintrag nennt die Zahl der Operationen, `undo_txn` verweigert bei weniger lesbaren Einträgen.
- **Veralteter Plan:** Schreib-Änderungen merken sich den SHA-256 der Ausgangsdatei; hat sich die Datei seit der Vorschau geändert (SM+, FTP, Nutzer), lehnt `apply_plan` ab, statt ein anderes Diff zu schreiben als gezeigt.
- **Whitelist:** zusätzliche Wurzeln aus Scanpfaden nur noch unter `/data/…`, `/mnt/usb<N>` und `/mnt/ext<N>` (vorher auch `/user/app`, `/system_data`, `/mnt/sandbox` …). Die Wurzeln stehen im Journal, `undo` nutzt sie zusätzlich zu den heutigen; es wird nie nach einem Pfad im Journal ohne Whitelist-Prüfung geschrieben oder aufgeräumt (Backup-Pfad muss unter `/data/ps5medic/backups` liegen).
- **Symlinks:** ein Symlink als Rename-Quelle wird abgelehnt, die Temp-Datei folgt keinem Symlink. **Backup-Ordner** werden nie wiederverwendet (auch wenn das Journal fehlt). **Journalfelder** mit falschem Typ und Pfade mit ungültigem UTF-8 werfen keine Ausnahme mehr.
- **Regel-Download:** Bei fehlgeschlagener Gegenprobe ohne Vorgängerdatei wird die neue Datei beiseite gelegt statt aktiv zu bleiben; scheitert der Log-Eintrag, wird der alte Stand wiederhergestellt (sonst gäbe es kein Rückgängig). Der Rollback-Schutz vergleicht auch mit der höchsten noch nicht zurückgenommenen Installation laut `update.log`, nicht nur mit der geladenen Version.
- **Nicht umgesetzt:** Rotation des Journals (es wächst jetzt linear statt quadratisch) und ein `realpath`-Abgleich für Ordner-Symlinks unterhalb der Wurzeln (setzt lokalen Schreibzugriff voraus).

