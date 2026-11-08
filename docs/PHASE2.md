# Phase 2 – Vorbereitung und erster Konsolenlauf

Stand 8. Nov. 2026. Alles hier ist am PC gebaut und getestet; **auf der Konsole wurde noch nichts ausgeführt.**

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

## UI-Entscheidung (Phase 3)
`PS5_VulkanTemplate` liegt lokal nicht vor. Lokal vorhanden sind `ps5-native-app-boilerplate-main` und `ps5-opengl-sdk` (Showcase PPSA99005). Ob Vulkan oder OpenGL auf FW 4.03 läuft, lässt sich nur auf der Konsole testen. Vorschlag: zuerst den Diagnose-Lauf auswerten, dann ein Grafik-Sample probieren.
