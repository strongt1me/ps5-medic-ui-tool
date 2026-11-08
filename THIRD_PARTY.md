# Third-Party

| Komponente | Version | Lizenz | Ort |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | `third_party/nlohmann/` (Lizenztext: `LICENSE.MIT`) |
| [TweetNaCl](https://tweetnacl.cr.yp.to/) | 20140427 | Public Domain (D. J. Bernstein u. a.) | `third_party/tweetnacl/` (unverändert; Quelle und Prüfsummen in `SOURCE.txt`; nur Ed25519 wird benutzt) |
| [QR Code generator library (C++)](https://www.nayuki.io/page/qr-code-generator-library) | Commit `bcfe1b0` (2026-10-29) | MIT, Project Nayuki | `third_party/qrcodegen/` (unverändert; Lizenztext steht im Kopf beider Dateien, Commit in `COMMIT.txt`) |

Schrift: Die UI enthält vorgerenderte Glyphen aus [DejaVu Sans](https://dejavu-fonts.github.io/) (Bitstream-Vera-Lizenz mit Public-Domain-Anteilen, frei weitergebbar) in `ui/src/font_data.cpp`. Erzeugt mit `tools/gen_font.py` (benötigt nur beim Neuerzeugen Pillow und die TTF-Dateien; beides wird nicht ausgeliefert).

Wissensbasis: [PS5-Jailbreak-Doctor](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor) (MIT, "Copyright (c) 2026 PS5 Jailbreak Doctor contributors", Repository von asomimlrxd). Seine `knowledge_base.json` (Version 2026.10.17.1) wurde gesichtet und diente als Hinweisgeber für die Regeln B16–B19 und die Erweiterung von B14 (Quellen, Fehlermuster, Einordnung); die Regeltexte, Bedingungen und Fixtures sind eigene Arbeit, die Quellen wurden einzeln bei GitHub nachgeprüft (Einzelheiten und was nicht übernommen wurde: `docs/RULES.md`, „Nachtrag: Doctor-Wissensbasis“). Dateien oder Texte des Doctors liegen nicht im Repo. Lizenztext des Doctors (MIT): Die Erlaubnis wird unentgeltlich erteilt, die Software und zugehörige Dokumentation ohne Einschränkung zu nutzen, zu kopieren, zu ändern, zusammenzuführen, zu veröffentlichen, zu verbreiten, unterzulizenzieren und zu verkaufen, unter der Bedingung, dass der Urheberrechtsvermerk und dieser Erlaubnishinweis in allen Kopien oder wesentlichen Teilen enthalten sind; die Software wird ohne jede Gewährleistung bereitgestellt (englischer Originaltext: https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/LICENSE.txt).

Nicht im Repo, nur zum Bauen benötigt: [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPLv3, John Törnblom) für `make ps5`. Der gebaute ELF linkt gegen dessen Laufzeit-Bibliotheken; deshalb steht PS5 Medic selbst unter GPL-3.0-or-later (siehe `LICENSE`). Die oben genannten Komponenten (MIT, zlib, BSD-2-Clause, DejaVu) sind damit vereinbar.

Kachel-Starter (`title/`): `title/title.ld` und `title/symbols.map` stammen aus [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) (GPL-3.0-or-later, BlackBearReloaded; der Kopf der Datei nennt das). `make title` nutzt dessen Werkzeug `ps5-native-tool` und kopiert den signierten Laufzeit-Shim `libc.prx` aus einem lokalen Checkout; beides liegt nicht im Repo, und der Titel ist nicht Teil von `make dist` (siehe `docs/TITLE.md`).

Laufzeit-Voraussetzung auf der Konsole, nicht im Repo und nicht von uns ausgeliefert: [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv) (GPL-3.0, John Törnblom u. a.) startet die UI über seinen Big-App-Lader (`/hbldr`, siehe `docs/PHASE3.md`). Der Launcher-Eintrag `ps5/homebrew/homebrew.js` ist eine eigene, kleine Datei nach dem Muster der Beispiele dieses Launchers.

Nicht im Repo, nur für `make ps5-ui` benötigt (Build-Schritte: `docs/PHASE3.md`):
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) (SDL2, zlib-Lizenz) – VideoOut/Pad-Anbindung. Statisch gelinkt (`libSDL2.a`). **Geänderte Version:** `src/video/ps5/SDL_ps5video.c` fordert 32 statt 64 MB Bildspeicher an (eine Zeile, siehe `docs/PHASE3.md`).
- [libsamplerate](https://libsndfile.github.io/libsamplerate/) 0.2.2 (BSD-2-Clause, Erik de Castro Lopo) – Abhängigkeit von SDL2s Audiopfad, auch wenn PS5 Medic selbst kein Audio ausgibt.
