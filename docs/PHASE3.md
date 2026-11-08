# Phase 3 – Vollbild-UI

Stand 8. Nov. 2026. Der plattformunabhängige Teil steht und ist am PC getestet. `make ps5-ui` baut `build-ps5/ps5medicui.elf` (SDL2 + Pad-Anbindung, siehe unten) – **auf der Konsole ist das noch nicht gelaufen.**

## Was schon da ist (`ui/`)
- `Canvas` (1920×1080, ARGB, Kantenglättung), Controller-Symbole als Vektorformen, PNG-Ausgabe für Vorschau und Tests.
- Schrift: DejaVu Sans, vorgerendert (`tools/gen_font.py` → `ui/src/font_data.cpp`). Umlaute, Latin-1 und alle Zeichen aus `rules.json`. Unbekannte Zeichen erscheinen als `?`. Textumbruch trennt auch lange Pfade ohne Leerzeichen.
- Texte in `ui/i18n/de.json` und `en.json`, eingebettet per `tools/embed_i18n.py` (ein Test vergleicht Datei und eingebetteten Text).
- `App`: neun Bildschirme (Start, Prüfung läuft, Befunde, Detail, Reparatur-Vorschau, Ergebnis, Verlauf, Rückgängig-Frage, Einstellungen). Bedienung wie in `SPEC.md`: ✕ öffnen/bestätigen, △ reparieren, ◯ zurück. □ „Bericht" fehlt noch (Phase 4).
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

**Spätere Option (Phase 4, optional):** Für einen echten Bibliotheks-Eintrag ließe sich ein winziger Title-„Starter" vorschalten, der nur `sceSystemServiceLoadExec` aufruft und sofort in den eigentlichen Payload wechselt – das Muster nutzt `strongt1me/ps5-usb-formatierer` bereits (`usb_format/app/title/starter.c`, C statt C++, minimale Importliste). Nicht nötig für die MVP-UI.

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

# 2. SDL2 (ps5-payload-dev-Fork, zlib-Lizenz)
git clone https://github.com/ps5-payload-dev/SDL.git && cd SDL
"$CMAKE" -DCMAKE_BUILD_TYPE=Release -DSDL_OPENGL=YES -DSDL_LOADSO=YES -B build -S .
"$MAKE" -C build && "$MAKE" -C build install

"$PS5_PAYLOAD_SDK/bin/prospero-cross-fix-root"
```
Landet in `$PS5_PAYLOAD_SDK/target/user/homebrew/{include,lib}` – das Makefile findet es dort automatisch (`PS5_HOMEBREW`). Am 8. Nov. 2026 so gebaut und erfolgreich mit `ps5medicui.elf` gelinkt; auf der Konsole noch nicht getestet.

**`ps5/ui_main.cpp`:** SDL-Fenster (1920×1080, Vollbild) ↔ `Canvas::data()` per `SDL_CreateRGBSurfaceWithFormatFrom`/`SDL_BlitSurface`, `SDL_GameController` ↔ `Button` (Tastenbelegung aus `SDL_ps5joystick.c`: a=Kreuz, b=Kreis, x=Viereck, y=Dreieck, start=Options, Schultertasten/Steuerkreuz direkt). `LocalService` mit `Ps5Platform(writable=true)`, Takt ~30 Bilder/s. Regeln aus denselben zwei Kandidatenpfaden wie der Diagnose-Payload.

```bash
make ps5-ui
make deploy-ui PS5_HOST=<ip>   # ps5medicui.elf + rules.json nach /data/homebrew/PS5MEDIC/
```

## Nächste Schritte
1. Konsolentest: startet der Payload per Payload Manager, zeigt er das Bild, reagiert der DualSense.
2. `CLAUDE.md` „Privilegien"-Abschnitt ist bereits auf den Payload-Ansatz angepasst (siehe Abschnitt „Korrektur" oben) – hier nur noch der Hinweis, falls beim Konsolentest etwas abweicht.
