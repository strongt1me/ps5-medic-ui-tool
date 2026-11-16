# Third-Party

| Komponente | Version | Lizenz | Ort |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | `third_party/nlohmann/` (Lizenztext: `LICENSE.MIT`) |
| [TweetNaCl](https://tweetnacl.cr.yp.to/) | 20140427 | Public Domain (D. J. Bernstein u. a.) | `third_party/tweetnacl/` (unverändert; Quelle und Prüfsummen in `SOURCE.txt`; nur Ed25519 wird benutzt) |
| [QR Code generator library (C++)](https://www.nayuki.io/page/qr-code-generator-library) | Commit `bcfe1b0` (2026-10-29) | MIT, Project Nayuki | `third_party/qrcodegen/` (unverändert; Lizenztext steht im Kopf beider Dateien, Commit in `COMMIT.txt`) |

Schrift: Die UI enthält vorgerenderte Glyphen aus [DejaVu Sans](https://dejavu-fonts.github.io/) (Bitstream-Vera-Lizenz mit Public-Domain-Anteilen, frei weitergebbar) in `ui/src/font_data.cpp`. Erzeugt mit `tools/gen_font.py` (benötigt nur beim Neuerzeugen Pillow und die TTF-Dateien; beides wird nicht ausgeliefert).

Wissensbasis: [PS5-Jailbreak-Doctor](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor) (MIT, "Copyright (c) 2026 PS5 Jailbreak Doctor contributors", Repository von asomimlrxd). Seine `knowledge_base.json` (Version 2026.10.17.1) wurde gesichtet und diente als Hinweisgeber für die Regeln B16–B19 und die Erweiterung von B14 (Quellen, Fehlermuster, Einordnung); die Regeltexte, Bedingungen und Fixtures sind eigene Arbeit, die Quellen wurden einzeln bei GitHub nachgeprüft (Einzelheiten und was nicht übernommen wurde: `docs/RULES.md`, „Nachtrag: Doctor-Wissensbasis“). Dateien oder Texte des Doctors liegen nicht im Repo. Lizenztext des Doctors (MIT): Die Erlaubnis wird unentgeltlich erteilt, die Software und zugehörige Dokumentation ohne Einschränkung zu nutzen, zu kopieren, zu ändern, zusammenzuführen, zu veröffentlichen, zu verbreiten, unterzulizenzieren und zu verkaufen, unter der Bedingung, dass der Urheberrechtsvermerk und dieser Erlaubnishinweis in allen Kopien oder wesentlichen Teilen enthalten sind; die Software wird ohne jede Gewährleistung bereitgestellt (englischer Originaltext: https://github.com/asomimlrxd/PS5-Jailbreak-Doctor/blob/main/LICENSE.txt).

Nicht im Repo, nur zum Bauen benötigt: [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPLv3, John Törnblom) für `make ps5`. Der gebaute ELF linkt gegen dessen Laufzeit-Bibliotheken; deshalb steht PS5 Medic selbst unter GPL-3.0-or-later (siehe `LICENSE`). Die oben genannten Komponenten (MIT, zlib, BSD-2-Clause, DejaVu) sind damit vereinbar.

Kachel-Starter (`title/`): `title/title.ld` und `title/symbols.map` stammen aus [ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) (GPL-3.0-or-later, BlackBearReloaded; der Kopf der Datei nennt das). `make title` nutzt dessen Werkzeug `ps5-native-tool` und kopiert den signierten Laufzeit-Shim `libc.prx` aus einem lokalen Checkout; beides liegt nicht im Repo, und der Titel ist nicht Teil von `make dist` (siehe `docs/TITLE.md`).

Im Release (`FAKE42007/sce_module/libc.prx`, Quelle der Kachel `make native-ui`): der signierte Laufzeit-Shim der nativen App, erzeugt mit [ps5-native-app-boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) (GPL-3.0-or-later, ursprünglich von BlackBearReloaded; das Repository nennt `libc_builder.cpp` und die Manifeste unter `tooling/native/runtime/` als unabhängig geschrieben und die erzeugte `libc.prx` als frei von Sony-Laufzeitcode). Die Datei liegt nicht im Repo, nur im Release-Zip; den Quelltext dazu bietet das Boilerplate-Repository an (GPL). Der Ordner heißt nach der Titel-ID `FAKE42007`, wie bei anderen Homebrew-Ordner-Apps.

Im Repo (`third_party/libdeflate/`): libdeflate 1.26 (MIT, Eric Biggers, Lizenztext in `COPYING`), unverändert, nur die zlib-Entpackung für x86-64 (`README.PS5MEDIC`); entpackt die PFSC-Blöcke der `.ffpfsc`-Images. `libmedic/src/imgread.c` und `libmedic/include/medic/imgread.h` stammen aus PS5 Cooling & System Center (derselbe Autor, GPL-3.0-or-later).

Im Repo (`third_party/stb/`): `stb_image.h` (Sean Barrett, stb_image 2.30, MIT oder Public Domain, Lizenztext in `third_party/stb/LICENSE`, Stand der Quelle in `UPSTREAM`), nur JPEG und PNG aus dem Speicher; entschlüsselt das Startbild (`ui/src/splash.cpp`). Die Kopie stammt aus dem Quelltext von PS5 Cooling & System Center (tvapp, GPL-3.0-or-later, unverändert).

Im Repo (`ps5/native_tilemap.inc`): die Kachelung des Bildspeichers, aus dem VideoOut-Treiber von [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) übernommen (Simple DirectMedia Layer, Copyright 1997-2018 Sam Lantinga, zlib-Lizenz; der Lizenzhinweis steht im Kopf der Datei). Gehört zur nativen App.

Übersetzungen: Die französischen, italienischen und spanischen Texte in `ui/i18n/` und `knowledge/l10n/` sind eigene Übersetzungen der deutschen und englischen Texte dieses Projekts (GPL-3.0-or-later).

Startbild: `ui/assets/splash.jpg` ist ein vom Autor bereitgestelltes Bild für die App. Hintergrund: `ui/assets/background.jpg` ist ein vom Autor selbst erstelltes Bild (Angabe von Marc, 16. Nov. 2026).

Im Repo (`third_party/websrv-hbldr/`): `hbldr.c/.h`, `elfldr.c/.h` und `pt.c/.h` aus [ps5-payload-dev/websrv](https://github.com/ps5-payload-dev/websrv), `src/ps5/` (Copyright John Törnblom, GPL-3.0-or-later, Lizenztext im Ordner; Stand der Quelle: Commit b9c9f88, 10. Okt. 2026). Das ist der Big-App-Lader, mit dem `ps5medic-launcher.elf` die UI mit App-Kontext startet (eine System-App wird per ptrace gegen die ELF getauscht). **Geändert** (nur `hbldr.c`): die Include-Zeilen und ein Zeitlimit von 20 s für `kevent()`, die Änderungen stehen im Kopf der Datei; `compat.h` ist eine eigene Hilfsdatei. Der Launcher selbst (`ps5/launcher_main.cpp`) ist eigener Code nach dem Muster von websrvs `src/ps5/sys.c` (Kachel per `sceAppInstUtilAppInstallTitleDir`). websrv selbst ist nicht mehr nötig; der Launcher-Eintrag `ps5/homebrew/homebrew.js` ist eine eigene, kleine Datei für Nutzer, die websrv ohnehin haben.

Nicht im Repo, nur für `make ps5-ui` benötigt (Build-Schritte: `docs/PHASE3.md`):
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) (SDL2, zlib-Lizenz) – VideoOut/Pad-Anbindung. Statisch gelinkt (`libSDL2.a`). **Geänderte Version:** `src/video/ps5/SDL_ps5video.c` fordert 32 statt 64 MB Bildspeicher an (eine Zeile, siehe `docs/PHASE3.md`).
- [libsamplerate](https://libsndfile.github.io/libsamplerate/) 0.2.2 (BSD-2-Clause, Erik de Castro Lopo) – Abhängigkeit von SDL2s Audiopfad, auch wenn PS5 Medic selbst kein Audio ausgibt.

## SQLite 3.54.1 (`third_party/sqlite3/`)

Gemeinfrei (Public Domain), unveraendert aus der Amalgamation von sqlite.org (Pruefsumme in `third_party/sqlite3/README.md`). Wird ohne Dateizugriff
uebersetzt und nur genutzt, um die Datenbanken der Systemsoftware im Arbeitsspeicher zu pruefen (`PRAGMA integrity_check` auf einer Kopie).

Der Aufbau dieser Pruefung (SQLite ohne Dateisystem-Teil, Datenbank per `sqlite3_deserialize` aus einer Kopie oeffnen, WAL-Modus der Kopie auf
Rollback-Journal umstellen) folgt der Idee von **PS5 Cleanup 1.0.7** (da_Chris86, https://github.com/xXBneutzerNameXx/PS5-Cleanup,
GPL-3.0-or-later, `db/db_sqlite.c` und `db/db_pruefen.c`). Der Code in `libmedic/src/sqlite_check.cpp` ist eigen geschrieben.

## PS5 DevLoop (nur Vertrag, kein Code)

Der Screenshot-Hook der UI (`ui/src/shot_hook.cpp`) folgt dem Vertrag aus der `AGENTS.md` von [PS5 DevLoop](https://github.com/griches/PS5_DevLoop) (Gary Riches, GPL-3.0-or-later): Anforderungsdatei entfernen, neues Bild in einen eigenen Ordner schreiben. Es wurde kein Code von DevLoop übernommen; Beschreibung und Vorbehalte in `docs/DEVLOOP.md`.
