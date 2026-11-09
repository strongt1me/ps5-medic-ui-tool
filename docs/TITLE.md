# Kachel und Start ohne websrv: der Launcher

Stand 9. Nov. 2026. **Eine ELF an Port 9021 senden, und PS5 Medic hat eine Kachel auf dem Startbildschirm und startet mit App-Kontext (DualSense).** Weder `websrv` noch ein Starter-Titel noch ShadowMount+ sind dafür nötig.

```bash
make send-launcher PS5_HOST=192.168.1.x    # baut ps5medic-launcher.elf und sendet es an Port 9021
```

Oder `ps5medic-launcher.elf` (liegt nach `make deploy-ui` in `/data/homebrew/PS5MEDIC/`) über den Payload Manager starten.

## Was der Launcher tut

```
ELF senden  ->  Kachel "PS5 Medic" (FAKE42008) installieren  ->  UI starten (wenn keine App läuft)  ->  bleibt als Dienst
Kachel öffnen  ->  Browser öffnet http://127.0.0.1:9056/  ->  Dienst startet die UI und schließt den Browser
```

1. **Kachel:** `ps5/launcher_main.cpp` legt `/user/app/FAKE42008/sce_sys/{param.json,icon0.png}` an und installiert sie über `sceAppInstUtilAppInstallTitleDir`, so wie websrv seine Kachel „Homebrew Launcher“ anlegt. Die Kachel hat keinen eigenen Code, nur einen `deeplinkUri` auf `http://127.0.0.1:9056/`. Das Icon kommt aus `/data/homebrew/PS5MEDIC/sce_sys/icon0.png` (`make deploy-ui`). Ist die Kachel schon da, bleibt sie.
2. **Dienst:** Der Launcher bleibt laufen und lauscht **nur auf 127.0.0.1:9056**. `GET /` (und `/launch`) startet die UI, `GET /quit` beendet den Dienst; alles andere ist 404.
3. **UI mit App-Kontext:** Der Start nutzt den Big-App-Lader aus websrv (`third_party/websrv-hbldr/`, GPL-3.0-or-later): Die System-App `FAKE00000` wird per `sceSystemServiceLaunchApp` gestartet und per ptrace gegen `ps5medicui.elf` getauscht. Das bringt App-Kontext (nur damit öffnet sich der DualSense, siehe `docs/PHASE3.md`) und keine Sandbox. Die System-App `/system_ex/app/FAKE00000` legt der Lader selbst an, falls sie fehlt.
4. **Vorher:** Der Launcher setzt wie websrv die System-`authid` des eigenen Prozesses (`0x4801000000000013`) und initialisiert den User-Dienst; beides ist Voraussetzung für `LaunchApp` und die Installation.

**Sicherheit:** Geschlossen wird nur, was ein System-Programm (`NPXS…`, der Browser der Kachel) oder eine Homebrew-Fake-App (`FAKE…`) ist. Ein Spiel oder eine App mit unbekannter Titel-ID wird nie geschlossen; dann zeigt der Launcher eine Benachrichtigung („Eine App läuft im Vordergrund. Bitte erst schließen.“). Es gibt keinen Netzzugriff nach außen.

**Neustart der Konsole:** Der Launcher läuft nicht weiter. Die Kachel bleibt, öffnet aber ohne Launcher nur eine leere Browserseite. Den Launcher erneut senden oder in die Autoload-Liste des Payload Managers eintragen (`/data/pldmgr/autoload.txt`, Eintrag `local/…`; das macht PS5 Medic nicht selbst).

**Neu senden:** Ein erneut gesendeter Launcher löst den laufenden ab (gleicher Thread-Name wie bei websrv, die alte Instanz wird beendet) und lässt eine laufende UI in Ruhe.

**Beenden der UI:** ◯ im Startbildschirm schließt die eigene App über das System (`sceSystemServiceKillApp` mit der App-ID aus `sceKernelGetAppInfo(getpid())`).

## Protokolle

- `/data/ps5medic/launcher.log`: Start, Ablösung der alten Instanz, Kachel-Installation (`AppInstallTitleDir rc=0` = ok), jeder UI-Start, welche App geschlossen wurde. Wird bei jedem Start überschrieben.
- `/data/ps5medic/ui.log`: die UI (wie bisher); `ui.stdout`: ihre Standardausgabe.

## Kachel entfernen

In den PS5-Speichereinstellungen „PS5 Medic“ (FAKE42008) entfernen. Der Ordner `PS5MEDIC` bleibt. Danach installiert ein erneut gesendeter Launcher sie wieder.

## Konsolentest (9. Nov. 2026, PS5 Pro FW 12.00)

- Erste Fassung des Launchers: Die UI startete nicht, `launcher.log` endete bei „hbldr_launch“. Ursache: Es fehlten `sceUserServiceInitialize` und die System-`authid`, die websrv in seinem Konstruktor setzt, und `kevent()` wartete ohne Zeitlimit. Behoben (Initialisierung, `authid`, 20-s-Limit in `hbldr.c`).
- Zweite Fassung: Die alte, hängende Instanz wurde per Pid aus dem alten Protokoll abgelöst, die Kachel `FAKE42008` über `AppInstallTitleDir` installiert (`rc=0`), die UI mit App-Kontext gestartet (`ui.log`: „Controller verbunden“, erstes Bild). Beim erneuten Senden bei laufender UI: alte Instanz per Thread-Name abgelöst, `Vordergrund: app_id 24600 (FAKE00000)` richtig erkannt, UI blieb unberührt.
- **Noch nicht geprüft:** das Öffnen der Kachel selbst (Browser → `127.0.0.1:9056` → UI-Start). Ob der Browser der Konsole die Adresse öffnet und welche Titel-ID er hat (`NPXS…` wird geschlossen, alles andere nicht), zeigt `launcher.log` beim ersten Mal.

## Der alte Weg (Starter-Titel, websrv)

Bis 8. Nov. gab es eine Kachel `FAKE42007`, einen winzigen nativen Titel (`title/starter.c`), der websrv per `/hbldr` bat, die UI zu starten (`make title`, `make deploy-title`; braucht ein ps5-native-app-boilerplate-Checkout und dessen `libc.prx`). Das ist **ersetzt**: der Launcher braucht weder websrv noch eine Titel-Toolchain. `title/` und die zugehörigen Make-Ziele bleiben als Ausweichweg im Repo; die alte Kachel lässt sich in den Speichereinstellungen entfernen. `make launch-ui` (websrv) funktioniert weiter.

## Lizenz

Der Lader in `third_party/websrv-hbldr/` ist GPL-3.0-or-later (John Törnblom), wie PS5 Medic; Änderungen und Herkunft stehen in `THIRD_PARTY.md` und im Kopf von `hbldr.c`. Der Launcher selbst ist eigener Code.
