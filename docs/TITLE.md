# Kachel und Start ohne Homebrew-Lader: Starter und Launcher

Stand 12. Nov. 2026. PS5 Medic ist eine **Ordner-App in `/data/homebrew/FAKE42007`**, wie alle anderen Apps und Spiele. ShadowMount+ erkennt den Ordner und legt die Kachel **PS5 Medic** unter „Medien“ an. Weder websrv noch ein Homebrew-Lader noch ein Browser sind nötig. Der Starter in der Ordner-App ist ein winziger nativer Titel (`eboot.bin`), so wie bei den Kacheln von „PS5 Cooling & System Center“: Er sendet den Starter-Launcher an den ELF-Lader (Port 9021), der Launcher startet die Vollbild-UI mit App-Kontext (nur damit öffnet sich der DualSense, siehe `docs/PHASE3.md`).

| Weg | Wie |
|---|---|
| **1. ELF senden** | `make send-launcher PS5_HOST=<ip>` (oder `ps5medic-launcher.elf` über den Payload Manager) schreibt den Ordner `/data/homebrew/FAKE42007` und startet die UI. ShadowMount+ legt die Kachel an. |
| **2. Ordner kopieren** | Den Ordner `FAKE42007` aus dem Release (oder `make deploy-title`) nach `/data/homebrew/` kopieren; ShadowMount+ legt die Kachel an, danach reicht ein Druck, ganz ohne ELF zu senden. |

Beide Wege ergeben denselben Ordner. Die UI selbst (`ps5medicui.elf`) liegt in `/data/homebrew/PS5MEDIC/` (`make deploy-ui`). Die Konsole selbst (`/system_ex`, `/user/app`) wird nie angefasst; die Einhängung macht ShadowMount+.

## Was beim Druck auf die Kachel passiert

```
Kachel  ->  Starter (eboot.bin, nativer Titel, sandboxed)  ->  sendet ps5medic-start.elf an 127.0.0.1:9021
        ->  Launcher (Payload)  ->  schließt den Starter (FAKE…-App)  ->  startet ps5medicui.elf mit App-Kontext
        ->  bleibt, bis die UI endet, und beendet sich dann
```

1. **Starter** (`title/starter.c`): freestanding, nur `libkernel`/`libSceSystemService`, die Launcher-ELF steckt per `.incbin` in der `eboot.bin`. Er braucht den ELF-Lader des Jailbreaks (Port 9021), sonst zeigt er eine Meldung. Gebaut mit dem **ps5-payload-sdk 0.45** und `ps5-native-tool` (ps5-native-app-boilerplate), Ablauf in `title/build_title.sh`.
2. **Launcher** (`ps5/launcher_main.cpp`): in zwei Fassungen aus einem Quelltext.
   - `ps5medic-start.elf` (steckt im Starter): startet nur die UI.
   - `ps5medic-launcher.elf` (die ELF zum Senden, `-DMEDIC_TILE_INSTALL`): schreibt zuerst die Dateien der Ordner-App (`eboot.bin`, `sce_module/libc.prx`, `sce_sys/icon0.png`, zuletzt `sce_sys/param.json`) nach `/data/homebrew/FAKE42007`, sofern sie dort nicht schon gleich sind, und startet dann die UI. Es werden nur eigene Dateien überschrieben, nichts gelöscht.
3. **UI mit App-Kontext:** Der Start nutzt den Big-App-Lader aus websrv (`third_party/websrv-hbldr/`, GPL-3.0-or-later, nur der Code, **nicht** der Dienst): Die System-App `FAKE00000` wird per `sceSystemServiceLaunchApp` gestartet und per ptrace gegen `ps5medicui.elf` getauscht. Vorher setzt der Launcher wie websrv die System-`authid` und initialisiert den User-Dienst. Wird gerade eine App geschlossen, wartet er kurz und versucht den Start bis zu dreimal.
4. **Kein Port, kein Netz:** Der Launcher lauscht auf nichts.

**Sicherheit:** Geschlossen wird nur, was ein System-Programm (`NPXS…`) oder eine Homebrew-Fake-App (`FAKE…`, darunter der Starter) ist. Ein Spiel oder eine App mit unbekannter Titel-ID wird nie geschlossen; dann zeigt der Launcher eine Benachrichtigung („Eine App läuft im Vordergrund. Bitte erst schließen.“).

## Die Ordner-App FAKE42007

`build-title/FAKE42007/` (im Release `FAKE42007/`) enthält `eboot.bin`, `sce_module/libc.prx` (signierter Laufzeit-Shim aus ps5-native-app-boilerplate, wie bei den anderen Homebrew-Ordner-Apps) und `sce_sys/{param.json,icon0.png}`; `applicationCategoryType 65536` heißt Kachel unter „Medien“.

Frühere Stände dieses Projekts legten eine zweite Kachel `FAKE42008` an (mit Programm unter `/system_ex/app`) oder öffneten über einen Link den Browser. Beides ist weg; eine noch vorhandene Kachel `FAKE42008` lässt sich in den Speichereinstellungen der PS5 entfernen.

## Bauen

```bash
make title                     # build-title/FAKE42007 (Starter mit eingebettetem Launcher)
make ps5-launcher              # ps5medic-launcher.elf (mit den Dateien der Ordner-App)
make deploy-ui deploy-title PS5_HOST=<ip>   # UI + Launcher nach PS5MEDIC, Ordner-App nach FAKE42007
make send-launcher PS5_HOST=<ip>            # Launcher senden (legt den Ordner an, startet die UI)
```

Voraussetzungen in WSL: **ps5-payload-sdk 0.45** (`PS5_PAYLOAD_SDK`, Standard `~/ps5-sdk-045/PS5_PAYLOAD_SDK`; `ps5-payload-sdk.zip` von github.com/ps5-payload-dev/sdk, SHA-256 `5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c`, mit SDL2 unter `target/user/homebrew`), `ps5-native-tool` (`PS5_NATIVE_TOOL`, Standard `~/lmms-ps5/boilerplate/build/host/ps5-native-tool`), `libc.prx` des Boilerplates (`PS5_LIBC_PRX`, Standard `~/bfapp/dist/PPSA77505/sce_module/libc.prx`) und `clang-18`.

`make ps5-launch-title` baut ein reines Entwicklungswerkzeug (`ps5medic-launch-title.elf`): Es startet einen Titel wie ein Druck auf die Kachel (Titel-ID in `/data/ps5medic/launch_title.txt`, Ergebnis in `launch_title.log`), damit der ganze Weg ohne Fernseher prüfbar ist.

## Protokolle

- `/data/ps5medic/launcher.log`: Start (mit Fassung), Ablösung der alten Instanz, Schreiben des Kachel-Ordners, jeder UI-Start samt Versuch, welche App geschlossen wurde. Wird bei jedem Start überschrieben.
- `/data/ps5medic/ui.log`: die UI; `ui.stdout`: ihre Standardausgabe.

## Neustart der Konsole

Die Kachel bleibt. ShadowMount+ und der ELF-Lader (Port 9021) müssen laufen; ein Launcher muss nicht vorher gesendet werden.

## Kachel entfernen

Den Ordner `/data/homebrew/FAKE42007` entfernen (macht PS5 Medic nie selbst); ShadowMount+ nimmt die Kachel dann weg.

## Lizenz

Der Lader in `third_party/websrv-hbldr/` ist GPL-3.0-or-later (John Törnblom), wie PS5 Medic; Änderungen und Herkunft stehen in `THIRD_PARTY.md` und im Kopf von `hbldr.c`. Starter und Launcher sind eigener Code. Das Bauverfahren des Starters (Linkerskript, `ps5-native-tool`, `libc.prx`) folgt ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later).
