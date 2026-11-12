<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Die Oberfläche als native App (Stand 12. Nov. 2026)

**Warum:** Die UI läuft bisher als geliehene Big App `FAKE00000` (der Lader von websrv steckt sie in die Hülle der PSNow-App). In dieser Hülle führt die PS-Taste nicht zum Homescreen (nur ein Ton, siehe `docs/AUDIT.md`). Eine native App mit eigenem Titel verhält sich wie jede andere App.

## Entwurf
Zwei Teile, weil eine sandboxed App `/data` und `/mnt` nicht lesen darf:
1. **UI-App** (`ps5/native_ui_main.cpp`, eigener Titel, Ordner-App in `/data/homebrew`): Bild über VideoOut, Pad über ScePad, ohne SDL, ohne Rechte-Anhebung.
2. **Dienst-Payload** (später, auf Basis von `ps5medic.elf`): liest die Konsole, führt Check-up und Reparaturen aus und antwortet der UI über einen Socket auf `127.0.0.1`. Die UI schickt ihn beim Start über Port 9021 an den ELF-Lader, falls er nicht läuft.

Die UI spricht nur über `IMedicService` (`ui/include/medic_ui/service.hpp`); der Dienst-Anschluss wird ein `RemoteService`. Die bisherigen `LocalService` und die SDL-UI bleiben als Ausweichweg erhalten, bis die native App alles kann.

## Bau
`make native-ui` (Skript `title/build_native_ui.sh`) baut `build-native/FAKE42009/`, `make deploy-native PS5_HOST=<ip>` legt es per FTP nach `/data/homebrew/FAKE42009` (ShadowMount+ legt die Kachel „PS5 Medic (nativ)" an). Grundlage: `ps5-native-tool`, `libc.prx` und die kleine C++-Laufzeit aus ps5-native-app-boilerplate, SDK 0.45.

Einschränkungen der Umgebung, die den Code prägen:
- **Keine Ausnahmen, kein RTTI.** Der Kern bleibt mit Ausnahmen gebaut; für die App wurden die benötigten Teile aufgeteilt (`libmedic/src/finding.cpp`: `to_string(Severity/TestStatus)`, `effective_confidence`, `urgency_of`) und der Rest ist reine UI (`ui/src/*` ohne `png.cpp`, `local_service.cpp`, `shot_hook.cpp`), dazu `diff.cpp`, `qr.cpp`, `checks.cpp`.
- **Kein iostream, kein Unwinder.** `qrcodegen` wird über `ps5/native_qrcodegen.cpp` ohne `ostringstream` und mit `throw` = Abbruch eingebunden; `ps5/native_shim.cpp` ersetzt die `_Unwind_*`-Einstiegspunkte des libc++abi.
- `libc++.a` und `libc++abi.a` des SDK werden statisch gelinkt (für `std::string` usw.).

## Stand
- **Meilenstein 1 (12. Nov. 2026 auf der Konsole bestätigt, FW 12.00):** Bild, Pad und **PS-Taste führt zum Homescreen** (in der geliehenen Big App `FAKE00000` ging sie nicht). Funde beim Test: (1) der App-Heap ist klein, große Blöcke gehen deshalb über eigene Speicherseiten (`ps5/native_runtime.cpp`); (2) der Bildspeicher ist gekachelt (`ps5/native_tilemap.inc`, aus dem SDL-Treiber); (3) beim Beenden mit der PS-Taste läuft die App im Hintergrund weiter, ihre Dateien bleiben bis zum Schließen gesperrt (FTP 550). Ursprünglicher Plan: UI mit Platzhalter-Dienst (`StubService`, meldet „Prüfung unvollständig"): Bild, Pad, Startbild, Hintergrund. Geprüft wurde: startet die App, zeigt sie das Bild, reagiert das Pad, **führt die PS-Taste zum Homescreen**, beendet Kreis die App.
- Meilenstein 2: Dienst-Payload mit Protokoll (JSON-Zeilen über localhost) für alle `IMedicService`-Aufrufe.
- Meilenstein 3: `RemoteService` in der UI, Ersetzen des Starters `FAKE42007`.

## Offene Risiken
- Die Importe der App (libc aus `libSceLibcInternal`, `libScePad`, `libSceVideoOut`) sind auf der Konsole noch nicht bestätigt; ein fehlender Import beendet die App vor `main()` ohne Meldung.
- `/data` ist aus der Sandbox nicht beschreibbar/lesbar; das Protokoll `native.log` entsteht dort nur, wenn ShadowMount+ es erlaubt.
- Lapy-Sandbox-Elevation (aus dem Boilerplate) wäre ein Weg, den Dienst einzusparen; sie ist nur auf FW 6.02 bis 12.70 bestätigt und wird bewusst nicht genutzt.
