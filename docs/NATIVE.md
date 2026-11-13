<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Die Oberfläche als native App (Stand 12. Nov. 2026)

**Warum:** Die UI läuft bisher als geliehene Big App `FAKE00000` (der Lader von websrv steckt sie in die Hülle der PSNow-App). In dieser Hülle führt die PS-Taste nicht zum Homescreen (nur ein Ton, siehe `docs/AUDIT.md`). Eine native App mit eigenem Titel verhält sich wie jede andere App.

## Entwurf
Zwei Teile, weil eine sandboxed App `/mnt` und die Systemordner nicht lesen darf:
1. **UI-App** (`ps5/native_ui_main.cpp`, eigener Titel, Ordner-App in `/data/homebrew`): zeigt nur an. Bild über VideoOut (gekachelter Bildspeicher), Pad über ScePad, ohne SDL und ohne Rechte-Anhebung. Sie hält die Bilder des Dienstes in einem Puffer und schickt den Pad-Zustand zurück.
2. **Dienst-Payload** (`ps5/service_main.cpp`, `make ps5-service`, `ps5medicservice.elf`): läuft über den ELF-Lader mit vollen Rechten, zeichnet die **ganze Oberfläche** (derselbe `App` + `LocalService` wie die SDL-UI, deshalb Prüfungen, Reparaturen, Undo, Regel-Update unverändert) und schickt nur die geänderten Bildstreifen (8 Zeilen Raster) auf `127.0.0.1:9060`.

Protokoll: `ps5/service_protocol.hpp` (Typ, Länge, Nutzlast; `H` Hallo, `B` Streifen, `P` Bild fertig, `Q` Ende; App → Dienst `S` Pad-Zustand, `X` beenden). Die App startet den Dienst bei Bedarf selbst: sie liest `/data/homebrew/PS5MEDIC/ps5medicservice.elf` und sendet ihn an Port 9021. Der Dienst endet mit der Oberfläche (Kreis auf dem Start), nach 120 s ohne App oder auf `X` einer neueren Instanz.

Entscheidung (12. Nov. 2026): Statt jeden `IMedicService`-Aufruf (über 30, viele Strukturen) zu übertragen, wird nur Bild und Eingabe übertragen. Das spart den größten Teil der Arbeit und hält die Logik an einer Stelle. Die SDL-UI (`ps5medicui.elf`) und `LocalService` bleiben als Ausweichweg erhalten.

## Bau
`make native-ui` (Skript `title/build_native_ui.sh`) baut `build-native/FAKE42009/`, `make deploy-native PS5_HOST=<ip>` legt es per FTP nach `/data/homebrew/FAKE42009` (ShadowMount+ legt die Kachel „PS5 Medic (nativ)" an). Grundlage: `ps5-native-tool`, `libc.prx` und die kleine C++-Laufzeit aus ps5-native-app-boilerplate, SDK 0.45.

Einschränkungen der Umgebung, die den Code prägen:
- **Keine Ausnahmen, kein RTTI.** Der Kern bleibt mit Ausnahmen gebaut; für die App wurden die benötigten Teile aufgeteilt (`libmedic/src/finding.cpp`: `to_string(Severity/TestStatus)`, `effective_confidence`, `urgency_of`) und der Rest ist reine UI (`ui/src/*` ohne `png.cpp`, `local_service.cpp`, `shot_hook.cpp`), dazu `diff.cpp`, `qr.cpp`, `checks.cpp`.
- **Kein iostream, kein Unwinder.** `qrcodegen` wird über `ps5/native_qrcodegen.cpp` ohne `ostringstream` und mit `throw` = Abbruch eingebunden; `ps5/native_shim.cpp` ersetzt die `_Unwind_*`-Einstiegspunkte des libc++abi.
- `libc++.a` und `libc++abi.a` des SDK werden statisch gelinkt (für `std::string` usw.).

## Stand
- **Meilenstein 1 (12. Nov. 2026 auf der Konsole bestätigt, FW 12.00):** Bild, Pad und **PS-Taste führt zum Homescreen** (in der geliehenen Big App `FAKE00000` ging sie nicht). Funde beim Test: (1) der App-Heap ist klein, große Blöcke gehen deshalb über eigene Speicherseiten (`ps5/native_runtime.cpp`); (2) der Bildspeicher ist gekachelt (`ps5/native_tilemap.inc`, aus dem SDL-Treiber); (3) beim Beenden mit der PS-Taste läuft die App im Hintergrund weiter, ihre Dateien bleiben bis zum Schließen gesperrt (FTP 550). Ursprünglicher Plan: UI mit Platzhalter-Dienst (`StubService`, meldet „Prüfung unvollständig"): Bild, Pad, Startbild, Hintergrund. Geprüft wurde: startet die App, zeigt sie das Bild, reagiert das Pad, **führt die PS-Taste zum Homescreen**, beendet Kreis die App.
- **Meilenstein 2 (12. Nov. 2026, UI erscheint auf der Konsole):** Dienst-Payload und Anzeige-App laufen; die App startet den Dienst selbst. Offen: Pad-Steuerung und Check-up mit echten Befunden bestätigen.
- **Startdauer (12. Nov. 2026, gemessen):** vom Start der App bis zum ersten Bild 2,2 s, dabei läuft der Balken des Startbilds mit dem Ladefortschritt des Dienstes. Gewonnen durch: Textrichtlinie mit Vorprüfung statt 12 Regex-Suchen je Text (Regeln 2,1 s -> 21 ms, Fehlercodes 1,4 s -> 7 ms) und Hintergrundaufbau ohne `hypot`. Rest: das erste Zeichnen der Oberfläche (~1,5 s).
- **Meilenstein 3 (12. Nov. 2026, nach Check-up-Test):** Die native App ist jetzt die Kachel `FAKE42007` „PS5 Medic“ (`make deploy-native` baut und legt sie samt `ps5medicservice.elf` nach `/data/homebrew`). `make dist` packt Kachel und Dienst statt Starter, Launcher und SDL-UI. Der alte Weg (Starter `title/starter.c`, Launcher mit hbldr, `ps5medicui.elf`) bleibt im Repository als Ausweichweg, wird aber von keiner Kachel mehr benutzt. README und TESTER.md (alle Sprachen) beschreiben seit dem 12. Nov. 2026 den Weg über die beiden Ordner `PS5MEDIC` und `FAKE42007`. Der Launcher zum Senden (`make send-launcher`, s. u.) legt Kachel und Dienst jetzt selbst an.

## Installer zum Senden (`make send-launcher`)
Ohne FTP: `make send-launcher PS5_HOST=<ip>` sendet `ps5medic-launcher.elf` (`ps5/installer_main.cpp`, rund 11 MB) an den ELF-Lader (Port 9021); `make dist` legt sie als `ps5medic-launcher.elf` ins Release. Die ELF trägt alle Dateien beider Ordner und schreibt sie nach `/data/homebrew/FAKE42007` (`eboot.bin`, `sce_module/libc.prx`, `sce_sys/icon0.png`, zuletzt `sce_sys/param.json`) und `/data/homebrew/PS5MEDIC` (`ps5medicservice.elf`, `rules.json`, `error_codes.json`, `sce_sys/icon0.png`). ShadowMount+ legt danach die Kachel an; der Installer startet nichts und braucht keine erhöhten Rechte.
- Jede Datei wird erst als `<name>.new` geschrieben und dann umbenannt, eine laufende App oder ein laufender Dienst sieht nie eine halbe Datei. Dateien, die schon gleich sind, bleiben unberührt; es wird nichts gelöscht.
- Protokoll: `/data/ps5medic/installer.log`, dazu eine Meldung auf dem Bildschirm. `make send-launcher` baut die App (`make native-ui`) und den Dienst zuerst neu, damit die ELF den aktuellen Stand trägt.
- Konsolentest 12. Nov. 2026 (FW 12.00): zweimal gesendet auf eine schon installierte Kachel; `eboot.bin`, Dienst, Icons und `param.json` wurden als schon aktuell erkannt, `rules.json` und `error_codes.json` neu geschrieben (Umbenennen funktioniert). Leere Installation am 13. Nov. 2026 an der Konsole geprüft (Testbau mit `make ps5-launcher INSTALL_ROOT=/data/ps5medic/install_test`, der in einen leeren Testordner schreibt): alle acht Dateien samt Unterordnern angelegt, keine `.new`-Reste; der Testordner wurde danach wieder entfernt.

## Offene Risiken
- Review vom 13. Nov. 2026 (bewusst nicht geändert): Der Dienst bedient eine App nach der anderen; ein neuer Dienst kann einen laufenden nicht ablösen, solange die App verbunden ist (`Address already in use`, ein Update wirkt erst nach Beenden der App). Der Port 9060 auf 127.0.0.1 ist unauthentifiziert: jeder lokale Prozess könnte Pad-Eingaben senden oder den Dienst beenden (nur lokal, jede Reparatur verlangt mehrere Eingaben, Backup und Undo).
- Die Importe der App (libc, `libScePad`, `libSceVideoOut`, `libSceUserService`) sind auf FW 12.00 bestätigt; auf anderen Firmwares ungeprüft, ein fehlender Import beendet die App vor `main()` ohne Meldung.
- Die App kann unter `/data/ps5medic/` Dateien anlegen, schreiben und lesen (bestätigt: `native.log`). Das Auflisten von Ordnern und `stat` sind laut Boilerplate-Doku aus der Sandbox verweigert, `/mnt` und die Systemordner bleiben unzugänglich: der Dienst-Payload bleibt nötig.
- Lapy-Sandbox-Elevation (aus dem Boilerplate) wäre ein Weg, den Dienst einzusparen; sie ist nur auf FW 6.02 bis 12.70 bestätigt und wird bewusst nicht genutzt.
