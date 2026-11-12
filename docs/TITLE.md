# Kachel und Start ohne Homebrew-Lader: Starter und Launcher

Stand 12. Nov. 2026. PS5 Medic hat **zwei Kacheln mit demselben Starter**, und keine braucht websrv, einen Homebrew-Lader oder einen Browser. Der Starter ist ein winziger nativer Titel (`eboot.bin`), so wie bei den Kacheln von „PS5 Cooling & System Center“: Er sendet den Starter-Launcher an den ELF-Lader (Port 9021), der Launcher startet die Vollbild-UI mit App-Kontext (nur damit öffnet sich der DualSense, siehe `docs/PHASE3.md`).

| Weg | Wie | Titel-ID |
|---|---|---|
| **1. ELF senden** | `make send-launcher PS5_HOST=<ip>` (oder `ps5medic-launcher.elf` über den Payload Manager) legt die Kachel an und startet die UI | `FAKE42008` |
| **2. Ordner-App** | Ordner `FAKE42007` nach `/data/homebrew/` kopieren (`make deploy-title`); ShadowMount+ legt die Kachel an, danach reicht ein Druck, ganz ohne ELF zu senden | `FAKE42007` |

Beide Kacheln heißen „PS5 Medic“ und tun dasselbe; man braucht nur eine. Die UI selbst (`ps5medicui.elf`) liegt in beiden Fällen in `/data/homebrew/PS5MEDIC/` (`make deploy-ui`).

## Was beim Druck auf die Kachel passiert

```
Kachel  ->  Starter (eboot.bin, nativer Titel, sandboxed)  ->  sendet ps5medic-start.elf an 127.0.0.1:9021
        ->  Launcher (Payload)  ->  schließt den Starter (FAKE…-App)  ->  startet ps5medicui.elf mit App-Kontext
        ->  bleibt, bis die UI endet, und beendet sich dann
```

1. **Starter** (`title/starter.c`): freestanding, nur `libkernel`/`libSceSystemService`, die Launcher-ELF steckt per `.incbin` in der `eboot.bin`. Er braucht den ELF-Lader des Jailbreaks (Port 9021), sonst zeigt er eine Meldung. Gebaut mit dem **ps5-payload-sdk 0.45** und `ps5-native-tool` (ps5-native-app-boilerplate), Ablauf in `title/build_title.sh`.
2. **Launcher** (`ps5/launcher_main.cpp`): in zwei Fassungen aus einem Quelltext.
   - `ps5medic-start.elf` (steckt im Starter): startet nur die UI.
   - `ps5medic-launcher.elf` (die ELF zum Senden, `-DMEDIC_TILE_INSTALL`): installiert zuerst die Kachel `FAKE42008` (Dateien des Starters stecken in der ELF), startet dann die UI.
3. **UI mit App-Kontext:** Der Start nutzt den Big-App-Lader aus websrv (`third_party/websrv-hbldr/`, GPL-3.0-or-later, nur der Code, **nicht** der Dienst): Die System-App `FAKE00000` wird per `sceSystemServiceLaunchApp` gestartet und per ptrace gegen `ps5medicui.elf` getauscht. Vorher setzt der Launcher wie websrv die System-`authid` und initialisiert den User-Dienst. Wird gerade eine App geschlossen, wartet er kurz und versucht den Start bis zu dreimal.
4. **Kein Port, kein Netz:** Der Launcher lauscht auf nichts (der frühere Dienst auf 127.0.0.1:9056 und die Browser-Kachel sind weg).

**Sicherheit:** Geschlossen wird nur, was ein System-Programm (`NPXS…`) oder eine Homebrew-Fake-App (`FAKE…`, darunter der Starter) ist. Ein Spiel oder eine App mit unbekannter Titel-ID wird nie geschlossen; dann zeigt der Launcher eine Benachrichtigung („Eine App läuft im Vordergrund. Bitte erst schließen.“).

## Die Kachel FAKE42008 (ELF senden)

Der Launcher legt sie so an wie das Beispiel `install_app` im ps5-payload-sdk (John Törnblom):

1. `/system_ex` beschreibbar machen (wie `hbldr_launch` es für `FAKE00000` tut),
2. `eboot.bin` und `sce_module/libc.prx` nach `/system_ex/app/FAKE42008/`,
3. `param.json` und `icon0.png` nach `/user/app/FAKE42008/sce_sys/` (und `param.json` auch nach `/system_ex/app/FAKE42008/sce_sys/`),
4. `sceAppInstUtilAppInstallTitleDir("FAKE42008", "/user/app/")`.

Ein reiner `/user/app/FAKE42008`-Ordner mit `eboot.bin` genügt nicht: Die Kachel erscheint, aber `sceSystemServiceLaunchApp` meldet `0x80940033` (nicht startbar). **Hinweis zu `CLAUDE.md`:** „Keine Schreibzugriffe auf `/system_ex`“ galt bisher für die Reparaturen; der Launcher schreibt dort nur den eigenen Ordner `FAKE42008` (wie schon `FAKE00000` beim UI-Start), einmalig und nur eigene Dateien. Sind alle Dateien schon auf dem Stand der ELF, wird nichts geschrieben und nichts installiert.

## Die Ordner-App FAKE42007

`build-title/FAKE42007/` (oder im Release `FAKE42007/`) enthält `eboot.bin`, `sce_module/libc.prx` (signierter Laufzeit-Shim aus ps5-native-app-boilerplate, wie bei den anderen Homebrew-Ordner-Apps) und `sce_sys/{param.json,icon0.png}`. Nach `/data/homebrew/FAKE42007/` kopieren, ShadowMount+ legt die Kachel unter „Medien“ an. Der Starter in ihr ist derselbe wie in `FAKE42008`.

## Bauen

```bash
make title                     # build-title/FAKE42007 und FAKE42008 (Starter mit eingebettetem Launcher)
make ps5-launcher              # ps5medic-launcher.elf (mit den Dateien der Kachel FAKE42008)
make deploy-ui deploy-title PS5_HOST=<ip>   # UI + Launcher nach PS5MEDIC, Ordner-App nach FAKE42007
make send-launcher PS5_HOST=<ip>            # Launcher senden (Kachel FAKE42008 + UI)
```

Voraussetzungen in WSL: **ps5-payload-sdk 0.45** (`PS5_PAYLOAD_SDK`, Standard `~/ps5-sdk-045/PS5_PAYLOAD_SDK`; `ps5-payload-sdk.zip` von github.com/ps5-payload-dev/sdk, SHA-256 `5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c`, mit SDL2 unter `target/user/homebrew`), `ps5-native-tool` (`PS5_NATIVE_TOOL`, Standard `~/lmms-ps5/boilerplate/build/host/ps5-native-tool`), `libc.prx` des Boilerplates (`PS5_LIBC_PRX`, Standard `~/bfapp/dist/PPSA77505/sce_module/libc.prx`) und `clang-18`.

`make ps5-launch-title` baut ein reines Entwicklungswerkzeug (`ps5medic-launch-title.elf`): Es startet einen Titel wie ein Druck auf die Kachel (Titel-ID in `/data/ps5medic/launch_title.txt`, Ergebnis in `launch_title.log`), damit der ganze Weg ohne Fernseher prüfbar ist.

## Protokolle

- `/data/ps5medic/launcher.log`: Start (mit Fassung), Ablösung der alten Instanz, Kachel-Installation (`AppInstallTitleDir rc=0` = ok), jeder UI-Start samt Versuch, welche App geschlossen wurde. Wird bei jedem Start überschrieben.
- `/data/ps5medic/ui.log`: die UI; `ui.stdout`: ihre Standardausgabe.

## Neustart der Konsole

Die Kacheln bleiben. Der ELF-Lader (Port 9021) muss laufen, sonst meldet der Starter „ELF-Lader nicht erreichbar“. Ein Launcher muss nicht vorher gesendet werden.

## Kachel entfernen

In den PS5-Speichereinstellungen „PS5 Medic“ entfernen (`FAKE42008`); `FAKE42007` verschwindet, wenn der Ordner aus `/data/homebrew/` gelöscht wird (macht PS5 Medic nie selbst).

## Lizenz

Der Lader in `third_party/websrv-hbldr/` ist GPL-3.0-or-later (John Törnblom), wie PS5 Medic; Änderungen und Herkunft stehen in `THIRD_PARTY.md` und im Kopf von `hbldr.c`. Starter und Launcher sind eigener Code. Das Bauverfahren des Starters (Linkerskript, `ps5-native-tool`, `libc.prx`) folgt ps5-native-app-boilerplate (BlackBearReloaded, GPL-3.0-or-later).
