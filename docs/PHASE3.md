# Phase 3 – Vollbild-UI

Stand 8. Nov. 2026. Der plattformunabhängige Teil steht und ist am PC getestet. `make ps5-ui` baut `build-ps5/ps5medicui.elf` (SDL2 + Pad-Anbindung, siehe unten). Auf der Konsole läuft die UI, wenn sie über websrvs Big-App-Lader gestartet wird (`make launch-ui`): laut `ui.log` kommen Bild und Controller (siehe „Erster Konsolenlauf"). Der ganze Ablauf ist am 8. Nov. auf der Test-Konsole durchgespielt (Check-up, Detail, Reparatur-Vorschau, Reparatur, Rückgängig, erneuter Check-up), belegt durch `ui.log` und das Journal; Phase 3 ist damit abgeschlossen.

## Was schon da ist (`ui/`)
- `Canvas` (1920×1080, ARGB, Kantenglättung), Controller-Symbole als Vektorformen, PNG-Ausgabe für Vorschau und Tests.
- Schrift: DejaVu Sans, vorgerendert (`tools/gen_font.py` → `ui/src/font_data.cpp`). Umlaute, Latin-1 und alle Zeichen aus `rules.json`. Unbekannte Zeichen erscheinen als `?`. Textumbruch trennt auch lange Pfade ohne Leerzeichen.
- Texte in `ui/i18n/de.json` und `en.json`, eingebettet per `tools/embed_i18n.py` (ein Test vergleicht Datei und eingebetteten Text).
- `App`: neun Bildschirme (Start, Prüfung läuft, Befunde, Detail, Reparatur-Vorschau, Ergebnis, Verlauf, Rückgängig-Frage, Einstellungen). Bedienung wie in `SPEC.md`: ✕ öffnen/bestätigen, △ reparieren, ◯ zurück. □ „Bericht" kam in Phase 4 dazu (`docs/PHASE4.md`).
- `IMedicService`: die einzige Schnittstelle der UI zum Kern. `LocalService` ruft den Kern im selben Prozess auf (PC-Vorschau, Tests, **und** auf der Konsole – siehe unten).
- Reparatur-Vorschau zeigt vorher genau, was geändert wird (Verschieben mit Quelle und Ziel, Schreiben mit Zeilen-Diff). Geschrieben wird nur nach ✕ auf der Vorschau, Rückgängig nur nach ✕ auf der Rückfrage. Die Tests prüfen, dass jede andere Taste nichts schreibt.
- Hinweis-Fixes (`*_hint`) zeigen eine Anleitung statt „Reparatur".

Vorschau am PC (schreibt PNGs, ändert nichts):
```bash
make preview SNAP=tests/fixtures/b8_pos OUT=build/preview
build/medic-ui-preview <snapshot> <ausgabe> --lang en --scale 2
```

## Korrektur: kein Jailbreak-Request nötig

Der erste Plan ging davon aus, PS5 Medic müsste ein richtiger, signierter Sony-„Title" sein (eigener Bibliotheks-Eintrag, `param.json`, FSELF) – mit allen Folgen: App-Sandbox, also kein Zugriff auf `/data`, und ein Weg, die Rechte anzuheben (Lapy-JB-Daemon oder etaHEN-API, siehe `CLAUDE.md`). Das war falsch.

**Der Diagnose-Payload aus Phase 2 beweist das Gegenteil:** `build-ps5/ps5medic.elf`, am 8. Nov. auf der Konsole gelaufen, liest `/data`, `/mnt/usb0` und `/mnt/shadowmnt` frei – ganz ohne Jailbreak-Request. Das geht, weil er über den Payload Manager/elfldr gestartet wird, nicht über die System-Bibliothek. Diese „Payloads" sind grundsätzlich nicht sandboxed; das ist das normale Modell für kstuff, ShadowMount+, den Payload Manager selbst und jedes andere Homebrew auf der Konsole.

Die Sandbox – und damit der Lapy/etaHEN-Umweg – gilt nur für echte Titles, gestartet aus der Spiele-Bibliothek. PS5 Medic braucht aber keinen Bibliotheks-Eintrag, nur einen Vollbildschirm. Also: **PS5 Medic wird als Payload gebaut**, mit der schon funktionierenden `make ps5`-Pipeline (ps5-payload-sdk, volles C++20 mit Regex/Filesystem/Streams – das baut bereits, siehe Phase 2). Ergebnis:
- kein Lapy-Daemon, keine Jailbreak-Request-Logik,
- keine fremde, eingeschränkte Build-Pipeline (ein zweiter Anlauf über `ps5-native-app-boilerplate` scheiterte genau daran: dessen minimaler Laufzeit-Stub verbietet laut eigener Doku Streams/Filesystem/Regex ohne separate Prüfung – exakt das, was `libmedic` braucht),
- `LocalService` bleibt die Service-Implementierung, auch auf der Konsole (kein `RemoteService`/Transport nötig).

`CLAUDE.md` nennt den Lapy/etaHEN-Weg noch als „Privilegien"-Plan; das ist jetzt überholt und wird bei Gelegenheit korrigiert.

**Nachtrag (Konsolentest, 8. Nov.): ein Punkt stimmte nicht ganz.** Ein über elfldr (Port 9021, auch Payload Manager) gestarteter Payload ist zwar unsandboxed, hat aber keinen **App-Kontext** (`app_id=0`), und ohne den lässt sich der DualSense nicht öffnen (`scePadOpen` → `0x809B0081`). Das Bild (VideoOut) geht auch ohne. Lösung ohne Title und ohne Jailbreak-Request: **websrvs Big-App-Lader** (`/hbldr`, ps5-payload-dev/websrv, GPL-3.0, läuft auf der Konsole, nicht Teil dieses Repos). Er startet über `sceSystemServiceLaunchApp("FAKE00000")` eine echte System-App und tauscht deren Inhalt per ptrace gegen den ELF aus: unsandboxed **und** mit App-Kontext. So laufen auch LakeSnes, Mednafen und FFplay mit Controller. Die Fake-App `/system_ex/app/FAKE00000` legt websrv beim ersten Homebrew-Start selbst an; auf der Test-Konsole existiert sie schon, PS5 Medic schreibt dort nichts. Ein Title-Starter wie `bfstarter` (`starter.c` sendet nur an Port 9021) hätte das Problem **nicht** gelöst; die frühere Idee „Title-Starter für einen Bibliothekseintrag" entfällt damit für das Pad, der Launcher-Eintrag (Kachel) kommt jetzt von `homebrew.js`.

**Nachtrag (9. Nov. 2026): websrv ist nicht mehr nötig.** Der Big-App-Lader (`hbldr.c`, `elfldr.c`, `pt.c`) steckt jetzt im Launcher `ps5medic-launcher.elf` (Port-9021-Payload, `ps5/launcher_main.cpp`, Quelle des Laders in `third_party/websrv-hbldr/`). Er legt zusätzlich die Kachel an und startet die UI bei Bedarf selbst. Aufbau, Sicherheitsregeln und Konsolentest: `docs/TITLE.md`. `make launch-ui` (websrv) bleibt als Ausweichweg.

## VideoOut- und Pad-Anbindung: SDL2

Entscheidung: **SDL2** über [`ps5-payload-dev/SDL`](https://github.com/ps5-payload-dev/SDL) (zlib-Lizenz, selbe Organisation wie das SDK) statt rohe `sceVideoOut*`/`scePad*`-Syscalls von Hand – fertige, getestete VideoOut-Implementierung statt Struktur-Layouts aus der Erinnerung nachzubauen.

**SDL2 für die PS5 bauen** (einmalig, nicht Teil von `make ps5-ui`, kein Netzzugriff beim normalen Bauen nötig):
```bash
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# 1. libsamplerate 0.2.2 (BSD-2-Clause) -- Abhaengigkeit von SDL2s Audiopfad
wget https://github.com/libsndfile/libsamplerate/releases/download/0.2.2/libsamplerate-0.2.2.tar.xz
tar -xf libsamplerate-0.2.2.tar.xz && cd libsamplerate-0.2.2
./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared
make && make install && cd ..

# 2. SDL2 (ps5-payload-dev-Fork, zlib-Lizenz) -- mit einer Aenderung (siehe unten): 32 statt 64 MB Bildspeicher
git clone https://github.com/ps5-payload-dev/SDL.git && cd SDL
sed -i 's|device_data->memsize = 0x4000000;|device_data->memsize = 0x2000000; /* ps5-medic: 32 statt 64 MB */|' \
    src/video/ps5/SDL_ps5video.c
"$CMAKE" -DCMAKE_BUILD_TYPE=Release -DSDL_OPENGL=YES -DSDL_LOADSO=YES -B build -S .
"$MAKE" -C build && "$MAKE" -C build install

"$PS5_PAYLOAD_SDK/bin/prospero-cross-fix-root"
```
Landet in `$PS5_PAYLOAD_SDK/target/user/homebrew/{include,lib}` – das Makefile findet es dort automatisch (`PS5_HOMEBREW`). Am 8. Nov. 2026 so gebaut und mit `ps5medicui.elf` gelinkt.

**Geändertes SDL (zlib-Klausel 2: geänderte Versionen kenntlich machen):** Der VideoOut-Treiber (`src/video/ps5/SDL_ps5video.c`) fordert beim Start fest 64 MB Direktspeicher an. Die Test-Konsole (PS5 Pro, FW 12.00) gab das nicht her (`EAGAIN`; 32, 24 und 16 MB gingen, für alle Speichertypen). Zwei 1080p-Bildpuffer brauchen nur 17 MB (`PS5_Tilemap_BufferSize`: 8,5 MB je Puffer), daher setzt der `sed`-Schritt oben `memsize` auf 32 MB. Sonst ist der Treiber unverändert.

## Erster Konsolenlauf (8. Nov. 2026)

Gefunden beim ersten Start auf der Test-Konsole (192.168.1.94):
- **Nichts sichtbar, keine Kachel:** Kachel ist Absicht (Payload, kein Title). Dass nichts erschien, lag am 64-MB-Speicherwunsch von SDL (siehe oben): `SDL_Init` scheiterte in `sceKernelAllocateMainDirectMemory`, der Payload hing danach unsichtbar in einer Endlosschleife.
- **Fehlersuche ohne Bildschirm:** `klog` (Port 3232) lieferte hier nichts. `stdout` des Payloads kommt nur auf der Sende-Verbindung zu Port 9021 an, und nur solange man sie offen hält (`shutdown(SHUT_WR)` statt `close`). Verlässlich ist die Logdatei `/data/ps5medic/ui.log` (per FTP holen, wird bei jedem Start überschrieben; ein zweiter Start überschreibt also das Log des laufenden ersten).
- **Alle Payloads heißen `payload.elf`** in der Prozessliste (elfldr vergibt den Namen), nicht wie die ELF-Datei. Ein „andere Instanzen von mir beenden" über den Prozessnamen ist deshalb nicht möglich.
- **Nur eine Instanz:** Solange eine UI läuft, hält sie VideoOut und Direktspeicher; eine zweite scheitert mit `sceVideoOutOpen: Device busy`. Über websrv beendet `/hbldr` die laufende Big-App selbst, bevor es die neue startet. Für Starts über Port 9021 trägt jede Instanz zusätzlich ihre PID in `/data/ps5medic/ui.owner` ein und beendet sich, sobald dort eine fremde PID steht (Prüfung alle 15 Bilder); die neue Instanz versucht `SDL_Init` bis zu 6 s lang erneut.
- **Bild lief, DualSense nicht (als Payload über elfldr):** `scePadOpen(user, 0, 0)` → `0x809B0081`, auch mit erfundenen Benutzer-IDs; Index 1 → `0x809B0003`, System-IDs 255/254/−1 → `0x809B0001` (Argumentprüfung). Der Benutzer wird gefunden (`sceUserServiceGetLoginUserIdList`), `sceUserServiceGetInitialUser` liefert aber `0x80940004`. Belege (Wegwerf-Sonden, nicht im Repo): `sceKernelGetAppInfo` meldet für den Payload `app_id=0`, leere Title-ID, `authid=0x480000001000000e` (wie `pldmgr.elf`), die Shell-Prozesse dagegen echte App-IDs (SceShellUI `NPXS40087`). Start über den Payload Manager ändert nichts (auch er startet über `elfldr`, ppid 103).
- **Ganzer Ablauf auf der Konsole bestätigt:** Das `ui.log` zeigt die Bildschirmfolge Start → Prüfung → Befunde (2) → Detail → Reparatur-Vorschau → Ergebnis → Prüfung → Befunde (1) → … → Befunde (0), dazu Verlauf → Rückgängig-Frage → Ergebnis. Das Journal `/data/ps5medic/journal.jsonl` hat die Transaktionen mit Intent, Op, Commit und Undo; die Dateien auf dem Stick stimmen mit dem Journal überein. Es wurden dabei **echte Änderungen am USB-Stick** gemacht und rückgängig gemacht: `…/homebrew/backport_aus/PPSA11111` → `…/homebrew/_duplicates/PPSA11111` (Regel `smp.duplicate_titleid`) und `Rennspiel (5).ffpfsc` → `Rennspiel 5.ffpfsc` (Regel `smp.parentheses_in_pfs_name`, nur Umbenennen, 60,5 GB, keine Kopie). Endzustand: beide Reparaturen angewendet, erneuter Check-up findet nichts; über „Verlauf" jederzeit rückgängig zu machen.
- **Gelöst über websrv `/hbldr`:** dieselbe Pad-Sonde, so gestartet, liefert `scePadOpen` → gültiges Handle, `scePadReadState` → `rc=0`, `connected=1`, `GetForegroundUser` funktioniert. Die UI meldet dort `Controller verbunden: Yolo` (Name des angemeldeten Benutzers) und gibt das erste Bild aus. Ablauf mit `make launch-ui PS5_HOST=<ip>` (Voraussetzung: `websrv.elf` läuft, Port 8080; aus dem Autoloader oder per Port 9021).
- **Beenden als App:** Über websrvs Lader ist die UI für das System eine laufende App. Endete nur der Prozess, blieb die App im Vordergrund hängen, und man kam nur mit PS-Taste und „Schließen" heraus. Die UI schließt beim Beenden jetzt ihre eigene App (`sceSystemServiceKillApp` mit der App-ID aus `sceKernelGetAppInfo(getpid())`; als Payload mit App-ID 0 passiert nichts, eine fremde App kann so nie getroffen werden). Siehe `docs/TITLE.md`.
- **Fehler gefunden und behoben: Rückgängig ohne Check-up.** Neue Sitzung, direkt in den Verlauf, Rückgängig → „Pfad nicht in der Whitelist": Die Whitelist kam aus den Scanpfaden der Fakten des letzten Check-ups, die in einer neuen Sitzung leer sind. `LocalService` sammelt die Fakten jetzt vor Reparatur und Rückgängig, falls noch keine da sind (`ensure_facts`); ein Test spielt die neue Sitzung nach (rot ohne den Fix, grün mit ihm).
- **Ausgang:** ◯ auf dem Startbildschirm beendet die App (`App::quit_requested()`), `SDL_Quit` gibt Bildschirm und Speicher frei. Bei Fehlern beendet sich der Payload mit Logeintrag, statt zu hängen.

**`ps5/ui_main.cpp`:** SDL-Fenster (1920×1080, Vollbild) ↔ `Canvas::data()` per `SDL_CreateRGBSurfaceWithFormatFrom`/`SDL_BlitSurface`, `SDL_GameController` ↔ `Button` (Tastenbelegung aus `SDL_ps5joystick.c`: a=Kreuz, b=Kreis, x=Viereck, y=Dreieck, start=Options, Schultertasten/Steuerkreuz direkt). `LocalService` mit `Ps5Platform(writable=true)`, Takt ~30 Bilder/s.

**Steuerung (seit 9. Nov. 2026):** Steuerkreuz **und linker Stick** wählen aus; die Richtungslogik steckt plattformunabhängig in `ui/include/medic_ui/pad_nav.hpp` (am PC getestet). Der Stick zählt ab rund 50 % Ausschlag (`kOn` 16000), fällt erst unter `kOff` 9000 zurück, nur die stärkere Achse gilt. Gehaltenes Hoch/Runter wiederholt nach 400 ms alle 110 ms (Steuerkreuz wie Stick); Links/Rechts bleiben Einzelschritte (sonst flackert die Sprache). **L1/R1** blättern: in Befundliste und Verlauf eine Seite, in den Details zum vorherigen/nächsten Befund (dort auch Links/Rechts), auf Start und in Einstellungen wie Hoch/Runter (auf Start springen Links/Rechts zwischen Verlauf und Einstellungen). Stick und Schultertasten bestätigen nie etwas: Reparieren und Zurücknehmen bleiben allein Kreuz (nach der Vorschau) vorbehalten. Die ersten Stick-Ausschläge stehen als `Stick-Achse …` in `ui.log`. Vorher wertete die UI nur das Steuerkreuz aus (Achsen-Ereignisse wurden nie gelesen, L1/R1 kamen an, aber `App::handle` ignorierte sie). Regeln aus denselben zwei Kandidatenpfaden wie der Diagnose-Payload.

```bash
make ps5-ui
make deploy-ui PS5_HOST=<ip>   # ps5medicui.elf, rules.json, homebrew.js und Icon nach /data/homebrew/PS5MEDIC/
make launch-ui PS5_HOST=<ip>   # deploy-ui + Start über websrv (App-Kontext, nötig fürs Pad)
```
Der Start geht auch ohne Make: im websrv-Launcher (`http://<ip>:8080`) erscheint die Kachel „PS5 Medic" (`ps5/homebrew/homebrew.js`, Icon aus `tools/gen_icon.py`).

## Nächste Schritte
1. Start ohne PC: Eintrag im Launcher reicht; websrv muss dafür laufen (im Autoloader der Test-Konsole als `websrv`-ELF vorhanden, nach einem Neustart prüfen).
2. Phase 4 (Bericht, Anonymisierung, QR-Code, README, Release-Paket): siehe `docs/PHASE4.md`. Die „PPSA-Ordner/param.json"-Installation aus dem Plan entfällt: der Launcher-Eintrag (`ps5/homebrew/`) ersetzt sie.
3. ~~Fehlende Regeln A2, A4, C1–C3~~ erledigt (22 Regeln, siehe `docs/RULES.md`, „Nachtrag").

## Nachtrag: Gestaltung mit Symbolen (11. Nov. 2026)

Die Oberfläche hat einen eigenen Symbolsatz (`ui/src/icons.cpp`, `ui/include/medic_ui/icons.hpp`): 31 Vektorsymbole (Linien, Kreise, Füllungen, Aussparungen), je Größe einmal in eine Deckkraftkarte gerechnet und danach nur noch eingefärbt. Keine Bilddateien, nichts wird geladen, keine Schrift nötig. `draw_icon_badge` setzt ein Symbol auf ein abgerundetes Farbfeld mit Verlauf. Der Symbolbogen zur Sichtprüfung entsteht mit `medic-ui-preview --icons <datei.png>`.

- **Start:** große Kachel mit Verlauf, Stethoskop und Herzschlaglinie; vier Kacheln (Verlauf, Einstellungen, Fehlercodes, Selbsttest) mit farbigem Symbolfeld; „Letzter Check-up“ mit großem Statussymbol (Haken, Warnung, Fehler, Info). Eine Teilprüfung bleibt blau („nicht alles gelesen“), nie grün.
- **Kopfzeile:** Logo (Kreuz im Farbfeld), Firmware-Chip. **Hinweisleiste** unten auf einer Fläche. **Hintergrund:** Verlauf mit zwei weichen Lichtflecken, gedithert (keine sichtbaren Stufen), einmal gebaut und je Bild kopiert.
- **Prüfung läuft:** Stethoskop und eine laufende Herzschlaglinie statt des Balkens.
- **Befunde und Detail:** Symbol je Schweregrad (Kritisch, Warnung, Info), Schraubenschlüssel bzw. Glühbirne bei Reparatur bzw. Anleitung. **Verlauf, Einstellungen, Fehlercodes, Ergebnis-Bildschirme:** Titel mit Symbolfeld, Statussymbole je Zeile.
- **Selbsttest:** je Test ein Symbol (Laufwerk, Thermometer, Datenbank, Datei, Tacho, WLAN, Flamme, Chip, Stift, Controller, Anzeige) und je Ergebnis ein Statussymbol (Haken, Warnung, Kreuz, Strich, offener Kreis). Zeilen sind enger, damit alle elf Tests über der Hinweisleiste Platz haben.
- Tests: `icon_tests` in `tests/test_ui.cpp` (jedes Symbol sichtbar, im Feld, Akzent, Verlauf).

## Nachtrag: Verlauf zurücksetzen (11. Nov. 2026)

Im Verlauf (Start → Verlauf) legt **Dreieck „Zurücksetzen“** nach einer Rückfrage (Kreuz bestätigt, Kreis bricht ab) das Reparatur-Journal (`journal.jsonl`) und den Check-up-Verlauf (`history.jsonl`) beiseite: Sie werden nach `journal_<Zeit>.jsonl` und `history_<Zeit>.jsonl` in `/data/ps5medic` **verschoben, nie gelöscht** (Leitlinie 4). Danach ist der Verlauf leer, und der nächste Check-up beginnt ohne Vergleich (alle Befunde „neu“, keine Vergleichswerte aus früheren Messungen). Die Sicherungen unter `backups/` bleiben; Reparaturen aus dem beiseite gelegten Journal lassen sich über die App nicht mehr rückgängig machen (die Rückfrage sagt das).

Schutz (`reset_history` in `libmedic/src/engine.cpp`): verweigert bei einer **unvollständigen Reparatur** (`incomplete`, die Engine braucht das Journal für den Rollback), ohne Schreibzugriff, bei ungültigem Zeitstempel und wenn eine Zieldatei schon existiert (nichts wird überschrieben). Ohne Verlauf ist es ein Nichts ohne Fehler. Tests: `history_reset_tests` in `tests/test_ui.cpp`.
