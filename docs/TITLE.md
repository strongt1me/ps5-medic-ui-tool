# Kachel unter „Medien"

Stand 8. Nov. 2026. PS5 Medic hat eine eigene Kachel auf dem Startbildschirm (Titel-ID `FAKE42007`, Kategorie *Medien*), zusätzlich zum Eintrag im websrv-Launcher.

## Wie es funktioniert

```
Kachel „PS5 Medic"  ->  Starter-Titel (7 KB)  ->  websrv  /hbldr  ->  ps5medicui.elf (Vollbild-UI, mit App-Kontext)
```

Der Titel selbst ist nur ein **Starter**: ein winziger nativer PS5-Titel (`title/starter.c`, freistehendes C, keine C-Bibliothek), der `websrv` auf `127.0.0.1:8080` bittet, die UI über dessen Big-App-Lader zu starten. Ein Titel darf weder `/data` lesen noch den DualSense öffnen (Sandbox); die UI braucht beides, und beides bekommt sie nur über websrvs Lader (Hintergrund: `docs/PHASE3.md`, „Nachtrag"). Ein Starter, der den Payload nur an Port 9021 schickt, würde nicht reichen, weil der Payload dann keinen App-Kontext hat.

`websrv` schließt beim Start die laufende Big App, und das ist der Starter selbst. Deshalb endet er normalerweise, bevor die Antwort kommt. Läuft `websrv` nicht, zeigt der Starter einen Hinweis und beendet sich.

**Beenden:** ◯ im Startbildschirm der UI schließt die eigene App über das System (`sceSystemServiceKillApp` mit der App-ID des eigenen Prozesses, gefragt per `sceKernelGetAppInfo(getpid())`). Vorher blieb die App nach dem Prozessende im Vordergrund hängen, und man kam nur mit PS-Taste und „Schließen" heraus. Als reiner Payload (App-ID 0) gibt es nichts zu schließen; eine fremde App, etwa ein laufendes Spiel, kann so nie getroffen werden.

## Voraussetzungen auf der Konsole

- `websrv` läuft (Autoloader oder Port 9021), getestet mit v0.33.
- Der Ordner `PS5MEDIC` (UI, Regeln) liegt in `/data/homebrew/`, siehe README.
- ShadowMount+ legt die Kachel an: Es scannt `/data/homebrew`, der Ordner `FAKE42007` wird wie ein Spiel installiert (`mount.lnk`, Icon). Das dauert beim ersten Mal etwa 10 Sekunden.

## Bauen und installieren

Der Starter braucht dieselbe Werkzeugkette wie dein BFpilot- und USB-Formatierer-Starter: ein Checkout von `ps5-native-app-boilerplate` (Standard `~/bfapp`, sonst `BOILERPLATE=<pfad>`) mit SDK 0.45 in `.deps` und gebautem `build/host/ps5-native-tool`, dazu `clang-18`. Das Skript `title/build_title.sh` übersetzt, linkt (mit `title/title.ld`), **prüft, dass nur die erlaubten Importe vorkommen**, wandelt um und signiert.

```bash
make title                               # baut build-title/FAKE42007/
make deploy-title PS5_HOST=192.168.1.x   # per FTP nach /data/homebrew/FAKE42007/
```

## Entfernen

Den Titel in den PS5-Speichereinstellungen entfernen und den Ordner `/data/homebrew/FAKE42007` verschieben oder löschen (per FTP). Der Ordner `PS5MEDIC` und der Launcher-Eintrag bleiben davon unberührt.

## Lizenz und Weitergabe

- `title/title.ld` ist aus `ps5-native-app-boilerplate/tooling/native/ps5-pie.ld` übernommen (GPL-3.0-or-later, BlackBearReloaded) und unverändert aus dem BFpilot-Starter kopiert; der Kopf der Datei nennt das.
- Zum Titel gehört `sce_module/libc.prx`, der **signierte Laufzeit-Shim des Boilerplates**. Er liegt weder im Repo noch im Release-Paket (`make dist`), sondern wird beim Bauen aus dem Boilerplate-Checkout kopiert.
- Deshalb ist der Titel **nicht Teil von `make dist`**. PS5 Medic steht unter GPL-3.0-or-later wie das Boilerplate, `starter.c` und das Bauskript sind damit lizenzgleich. Der Titel bleibt trotzdem aus dem Paket: `libc.prx` ist ein fremder, signierter Shim, den wir nicht weitergeben.

## Bekannte Eigenheiten

- Startet man die Kachel, während schon eine andere Homebrew-App über websrv läuft, kann es zu Fehlern kommen (bekannt von websrv).
- Beendet sich ein Titel genau in dem Moment, in dem websrv etwas startet, hält ShadowMount+ ihn mitunter noch für aktiv und verweigert spätere Starts („Device busy"; erst ein Neustart hilft). Der Starter wartet deshalb kurz, bevor er sich beendet. Im ersten Konsolentest trat das nicht auf (siehe unten).

## Konsolentest (8. Nov. 2026)

Kachel geöffnet, UI benutzt, im Startbildschirm mit ◯ beendet. Das ShadowMount+-Log zeigt die Kette lückenlos: der Starter (`FAKE42007`) endet („awaiting same-app handoff"), im selben Moment startet `FAKE00000` als App 0x18 mit der UI, nach ◯ steht „game stopped … title=FAKE00000" und „finalizing stopped application", also ein sauberes Schließen durch das System. Kein „Device busy", der Starter-Titel wurde danach ohne Neustart erneut gestartet. `ui.log` hält fest: „Beenden: schließe die eigene App (app_id 24)".
