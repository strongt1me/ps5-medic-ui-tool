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
| A2 `hen.jb_too_early` | Uptime < 60 s beim JB-Start (aus Log-Zeitstempeln) | Nach dem Einschalten ca. 1 Minute warten, bevor der Jailbreak läuft. | Hinweis | [kstuff-lite #94](https://github.com/EchoStretch/kstuff-lite/issues/94) | community |
| A3 `hen.etahen_double_load` | zwei etaHEN-Prozesse | Keine zweite etaHEN-Kopie über eine laufende laden; vorher neu starten. | Hinweis | [etahen-13.60 Releases](https://github.com/GronedWaffel/etahen-13.60/releases) | doc |
| A4 `payload.port_9021_closed` | elfldr-Prozess fehlt / Port 9021 nicht offen | Kein ELF-Loader aktiv → Payloads können nicht gesendet werden. Abhängig vom Autoloader. | Hinweis | eigene Erfahrung (Unified JB) | todo |

## B. ShadowMount+ (SM+)

| ID | Erkennung | Klartext / Empfehlung | Fix | Quelle | Status |
|---|---|---|---|---|---|
| B1 `smp.kstuff_min_version` | kstuff-lite < 1.07 | SM+ benötigt kstuff-lite ab v1.07. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B2 `smp.parentheses_in_pfs_name` | `.ffpfs`/`.ffpfsc` mit `(` oder `)` im Namen | Klammern im Dateinamen führen beim Scan zur Kernel-Panic; umbenennen behebt es. | `rename_image_parentheses` | [SM+ #66](https://github.com/drakmor/ShadowMountPlus/issues/66) | community |
| B3 `smp.autotoggle_crash_1360` | FW 13.60 + SM+ 1.7beta3 + kstuff-lite 1.11 + `kstuff_game_auto_toggle=1` + wiederkehrende Spielabstürze | Workaround: `kstuff_game_auto_toggle=0` (kstuff bleibt geladen). Erst config.ini sichern. | `smp_set_key` | [SM+ #130](https://github.com/drakmor/ShadowMountPlus/issues/130) | community |
| B4 `smp.fw900_kstuff_fork` | FW 9.00 + Dumps starten nicht („can't launch the game or app“) + kstuff-lite von EchoStretch | Nutzer berichten: Drakmors kstuff-Fork statt EchoStretch-Version löst es. | Hinweis | [SM+ #89](https://github.com/drakmor/ShadowMountPlus/issues/89) | community |
| B5 `smp.backpork_conflict` | BackPork-Prozess läuft **und** `backport_fakelib=1` (Standard) | BackPork und SM+-Fakelib vertragen sich nicht – BackPork deaktivieren. | `smp_disable_conflicting_backpork` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B6 `smp.image_extra_folder` | Image enthält `/<ORDNER>/sce_sys/param.json` statt `/sce_sys/param.json` | Spieldateien müssen im Image-Root liegen, ohne zusätzlichen Ordner. Image neu bauen. | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B7 `smp.ffpfsc_direct_files` | `.ffpfsc` enthält direkt Spieldateien statt verschachteltem Image | `.ffpfsc` ist nur ein Container für ein inneres Image (`.exfat`/`pfs_image.dat`). | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B8 `smp.duplicate_titleid` | gleiche TITLE_ID aus mehreren Quellen | Pro TITLE_ID nur eine Quelle behalten. | `move_duplicate_titleid` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B9 `smp.custom_scanpath_hides_defaults` | mind. eine `scanpath=`-Zeile **und** Spiele liegen in Standardpfaden | Sobald eigene `scanpath=` gesetzt sind, werden nur noch diese gescannt. | `smp_set_key` (Pfad ergänzen) | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B10 `smp.scan_depth` | Spiele liegen 2 Ebenen tief, `scan_depth=1` | `scan_depth=2` setzen oder Ordner flacher anlegen. | `smp_set_key` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B11 `smp.source_not_stable` | `debug.log` enthält `source not stable yet` wiederholt | Kopiervorgang noch nicht fertig oder `stability_wait_seconds` zu hoch/niedrig. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B12 `smp.exfat_cluster` | `.exfat`-Image mit Cluster < 64 KB | Langsamer; Images mit 64-KB-Clustern bauen. | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B13 `smp.debug_off` | `debug=0` | Ohne Debug-Log keine Diagnose möglich – für die Fehlersuche `debug=1`. | `smp_set_key` | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B14 `smp.register_failed` | `debug.log`: `Register failed` | Launcher-Icon entfernen/neu registrieren; als letzter Schritt Spieldaten löschen (Achtung: Saves weg). | Anleitung | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |
| B15 `smp.image_sector_autotune` | `autotune.ini` enthält neuen `image_sector`-Eintrag | SM+ hat die Sektorgrösse angepasst – Spiel einfach erneut starten. | Hinweis | [SM+ README](https://github.com/drakmor/ShadowMountPlus) | doc |

## C. Plugins & Speicher

| ID | Erkennung | Klartext / Empfehlung | Fix | Quelle | Status |
|---|---|---|---|---|---|
| C1 `plugin.ps5theme_old_overlay` | ps5-theme-Version älter als 1.23 + Fehler CE-105733-9 | Alte ps5-theme-Testbuilds verhinderten jeden App-Start; aktualisieren. | Hinweis | [ps5-theme 1.53](https://github.com/Blaze414/ps5-theme/releases/tag/v1.53) | todo (Versionsgrenze prüfen) |
| C2 `storage.oversized_reservation` | Homebrew-App belegt mehrere GB, obwohl die Dateien klein sind | Überdimensionierte Speicherreservierung; App über die PS5-Speichereinstellungen entfernen und neu installieren. | Hinweis | [CD Player 1.1.2](https://prosperostore.net/app/PPSA98427/) | doc |
| C3 `storage.low_space_data` | `/data` < 5 % frei | Wenig Platz: Mounts, Logs und Installationen können scheitern. | Hinweis | allgemein | todo |

## Erweiterung
1. Wissensbasis des Windows-Doctors (MIT) sichten: [knowledge_base.json](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/knowledge_base.json) und [FONTES_PT.md](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/FONTES_PT.md). Passende Regeln übernehmen (Lizenzhinweis in `THIRD_PARTY.md`).
2. Issues regelmässig auswerten: [SM+ Issues](https://github.com/drakmor/ShadowMountPlus/issues), [kstuff-lite Issues](https://github.com/EchoStretch/kstuff-lite/issues), GBAtemp-PS5-Forum.
3. Eigene Fälle von der FW-4.03-Konsole als Fixtures sichern.
