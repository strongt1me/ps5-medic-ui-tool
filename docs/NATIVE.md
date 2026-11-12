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
- Meilenstein 3: Die native App ersetzt den Starter `FAKE42007` (gleiche Kachel, Titel-ID und Start über die Kachel), Aufräumen der Testkachel `FAKE42009`.

## Offene Risiken
- Die Importe der App (libc, `libScePad`, `libSceVideoOut`, `libSceUserService`) sind auf FW 12.00 bestätigt; auf anderen Firmwares ungeprüft, ein fehlender Import beendet die App vor `main()` ohne Meldung.
- Die App kann unter `/data/ps5medic/` Dateien anlegen, schreiben und lesen (bestätigt: `native.log`). Das Auflisten von Ordnern und `stat` sind laut Boilerplate-Doku aus der Sandbox verweigert, `/mnt` und die Systemordner bleiben unzugänglich: der Dienst-Payload bleibt nötig.
- Lapy-Sandbox-Elevation (aus dem Boilerplate) wäre ein Weg, den Dienst einzusparen; sie ist nur auf FW 6.02 bis 12.70 bestätigt und wird bewusst nicht genutzt.
