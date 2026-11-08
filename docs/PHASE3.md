# Phase 3 – Vollbild-UI

Stand 8. Nov. 2026. Der plattformunabhängige Teil steht und ist am PC getestet. **Auf der Konsole läuft die UI noch nicht.** Die Architektur-Entscheidung steht jetzt fest (siehe „Korrektur" unten); offen ist nur noch der Bildschirm-/Eingabe-Adapter.

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

## Offen: VideoOut- und Pad-Anbindung

Für den eigentlichen Bildschirm/Eingabe-Code gibt es zwei Wege:
1. **SDL2** über `ps5-payload-dev/SDL` (pacbrew-Paket `ps5-payload-sdl2`, GPL/LGPL, selbe Organisation wie das SDK). Das ist der in `CLAUDE.md` genannte Plan-B-Ansatz. Vorteil: fertige, getestete VideoOut-Implementierung, kein Nachbau der rohen `sceVideoOut*`-Aufrufe aus dem Gedächtnis. Nachteil: Build aus Quelle (CMake, `ps5-payload-libsamplerate` als Abhängigkeit), lokal noch nicht versucht.
2. **Rohe `sceVideoOut*`/`scePad*`-Syscalls** von Hand, wie es z. B. `ps5-native-app-boilerplate` vormacht. Das wäre schneller, aber die genauen Struktur-Layouts (reservierte Felder, Pixelformat-Konstanten) müssten zuverlässig verifiziert werden statt aus der Erinnerung übernommen – sonst drohen schwer zu findende Fehler, die sich nur auf der echten Konsole zeigen.

Empfehlung: **SDL2**, weil es schon im Projekt vorgesehen ist (`CLAUDE.md`) und das Risiko falscher Low-Level-Annahmen entfällt.

## Nächste Schritte
1. `ps5-payload-sdl2` bauen (pacbrew) oder eine fertige Kopie finden.
2. `ps5/ui_main.cpp`: SDL2-Fenster/Surface ↔ `Canvas::data()`, `SDL_GameController`/`SDL_PollEvent` ↔ `Button`, `LocalService` mit `Ps5Platform`.
3. Konsolentest: startet der Payload per Payload Manager, zeigt er das Bild, reagiert der Controller.
4. `CLAUDE.md` „Privilegien"-Abschnitt an den Payload-Ansatz anpassen.
