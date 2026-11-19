# Phase 2 – Vorbereitung und erster Konsolenlauf

**Deutsch** · [English](en/PHASE2.md)

Stand 8. Nov. 2026. Der Diagnose-Payload ist am selben Tag zum ersten Mal auf einer Konsole gelaufen; die Ergebnisse stehen unten im Abschnitt „Ergebnis des ersten Laufs“. Die Tabelle „Offene Punkte“ beschreibt den Stand **vor** diesem Lauf.

## Was schon da ist
- `make ps5` baut `build-ps5/ps5medic.elf` (FreeBSD-x86-64-ELF, ~2 MB) mit dem ps5-payload-sdk. Das SDK liegt **nicht** im Repo: Standardpfad `~/ps5-sdk/PS5_PAYLOAD_SDK` (WSL), überschreibbar mit `PS5_PAYLOAD_SDK=...`. Quelle der Kopie: `PS5 SDK usw\PS5_PAYLOAD_SDK` (GPLv3, John Törnblom). Die Wrapper-Skripte benutzen das System-clang/lld der WSL-Distribution.
- `ps5/ps5_platform.cpp`: `Ps5Platform` mit echtem Dateisystem (`/`), Prozessliste per `sysctl kern.proc.proc`, Firmware aus `kernel_get_fw_version()`, Modell, Speicher per `statfs`.
- `ps5/main.cpp`: Diagnose-Payload. Sammelt, wertet Regeln aus, schreibt Befunde nach stdout und klog, dazu `/data/ps5medic/last_check.txt` und einen **Snapshot** nach `/data/ps5medic/snapshots/<zeit>/`. Er repariert nichts.
- Snapshot-Export (`libmedic/src/snapshot_export.cpp`): schreibt den Zustand im Fixture-Format. Test: alle 43 Fixtures ergeben nach Export und erneutem Einlesen dieselben Fakten und Befunde. `medic-cli export <ordner> <ziel>` macht dasselbe am PC.

## Erster Lauf (du, an der Konsole)
```bash
make ps5
make deploy PS5_HOST=<ip>      # FTP-Server (Port 2121) muss laufen
```
1. `ps5medic.elf` liegt danach in `/data/homebrew/PS5MEDIC/`, die Regeln in `/data/ps5medic/knowledge/rules.json`.
2. ELF über den Payload Manager starten (Port 9021 ist beim Unified JB zu).
3. Per FTP holen: `/data/ps5medic/last_check.txt` und den Ordner `/data/ps5medic/snapshots/<zeit>/`.
4. Am PC: `build/medic-cli check <snapshot-ordner>`.

Der Snapshot enthält Datei- und Ordnernamen (z. B. Spieltitel), aber keine Spielinhalte, Seriennummern oder IPs. Vor dem Teilen ansehen.

## Offene Punkte, die der erste Lauf klären soll
| Frage | Wo sichtbar | Folge |
|---|---|---|
| Stimmt die FW-Umrechnung (`0x04030000` → `4.03`)? | Zeile „Firmware:“ | sonst `fwver.hpp` anpassen |
| Wie heissen die Prozesse wirklich (`etaHEN`, `kstuff`, `shadowmountplus`, `backpork`)? | Liste „Prozesse:“, `procs.json` | Regeln A1/A3/B1/B5 vergleichen Namen exakt |
| Darf die Payload `/data` und `/mnt/*` lesen? | leere Pfade im Snapshot, Fehler im klog | sonst Root-Verzeichnis setzen (Muster: SDK-Sample `list_files`) |
| Woher kommen Versionen von kstuff, ShadowMount+, etaHEN? | `components` in `system.json` ist leer | ohne Quelle feuern B1, B3, B4 nicht |
| Existiert `/mnt/shadowmnt/...` wie im README beschrieben? | Snapshot unter `fs/mnt/shadowmnt` | B6/B7 |
| Welche Autoload-Liste nutzt die Konsole? | Snapshot enthält `autoload.txt`, falls vorhanden | B5-Fix |

## Ergebnis des ersten Laufs (8. Nov. 2026)
Konsole: `CFI-7021 B01Y` (vermutlich PS5 Pro), Firmware 12.00. **`CLAUDE.md` nennt als Test-Konsole FW 4.03; diese Konsole weicht davon ab.** Die FW-Umrechnung ist bestätigt: Das `debug.log` von ShadowMount+ meldet selbst „FW: 12.00“.

| Frage | Ergebnis | Folge im Code |
|---|---|---|
| FW-Umrechnung | `12.00` stimmt (SM+ meldet dasselbe) | keine Änderung |
| Prozessnamen | tragen die Endung `.elf` (`kstuff.elf`, `shadowmountplus.elf`, `pldmgr.elf`, `elfldr.elf`, …); kein etaHEN | Collector entfernt `.elf`, sonst hätten A1/A3/B1/B4/B5 nie gefeuert |
| Lesezugriff | `/data` und `/mnt/usb0` lesbar, auch `/data/shadowmount`, `/data/pldmgr` | keiner nötig |
| `/mnt/shadowmnt` | existiert, Layout wie im README (`<name>_<hash>`, `pfsc`) | keine Änderung |
| Versionen | `components` bleibt leer. SM+-Version steht im `debug.log` (`ShadowMount+ v1.7beta11`) | Collector liest `smp.version` aus dem `debug.log` (jüngste Startzeile). kstuff und etaHEN: weiter keine Quelle, B1/B4 feuern dort nicht |
| Autoload-Liste | `/data/pldmgr/autoload.txt`, Einträge als `local/<name>` ohne Endung | B5-Fix trifft per Regex, passt |
| Befunde | B2 (Klammern) und B8 (doppelte TITLE_ID) waren echt. **B10 war falsch positiv**: `sce_sys/npbind.dat` in einem Ordner ohne `param.json` zählte als `.dat`-Image in großer Tiefe | Collector überspringt `sce_sys`; Fixture `b10_neg_sce_sys_dat` |

Neue Regressions-Fixtures (synthetisch): `a1_pos_elfnames`, `b10_neg_sce_sys_dat`, `b3_pos_logversion`, `b3_neg_logversion`. Der echte Snapshot kommt **nicht** ins Repo: er enthält Dateinamen aus der Sammlung des Nutzers.

Praktisches:
- Der Payload gibt seine Ausgabe über dieselbe Verbindung zurück, wenn man ihn an Port 9021 sendet (`nc <ip> 9021 < build-ps5/ps5medic.elf`). Auf dieser Konsole lauscht Port 9021, anders als in `CLAUDE.md` angenommen.
- Snapshot holen: `wget -r -l inf -nH -np ftp://<ip>:2121/data/ps5medic/snapshots/<zeit>/` – ohne `-l inf` kappt wget bei Tiefe 5 und die Spielordner (`fs/mnt/usb0/homebrew/<spiel>/sce_sys/…`) fehlen.

Noch offen: Quelle für die kstuff-Version; ob `api_bind_address=0.0.0.0` in der SM+-Config (API im LAN erreichbar) einen Hinweis-Befund wert ist.

## UI-Entscheidung (Phase 3)
`PS5_VulkanTemplate` liegt lokal nicht vor. Lokal vorhanden sind `ps5-native-app-boilerplate-main` und `ps5-opengl-sdk` (Showcase PPSA99005). Ob Vulkan oder OpenGL auf FW 4.03 läuft, lässt sich nur auf der Konsole testen. Vorschlag: zuerst den Diagnose-Lauf auswerten, dann ein Grafik-Sample probieren.
