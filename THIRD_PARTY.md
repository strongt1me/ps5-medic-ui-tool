# Third-Party

| Komponente | Version | Lizenz | Ort |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.11.3 | MIT | `third_party/nlohmann/` (Lizenztext: `LICENSE.MIT`) |

Schrift: Die UI enthält vorgerenderte Glyphen aus [DejaVu Sans](https://dejavu-fonts.github.io/) (Bitstream-Vera-Lizenz mit Public-Domain-Anteilen, frei weitergebbar) in `ui/src/font_data.cpp`. Erzeugt mit `tools/gen_font.py` (benötigt nur beim Neuerzeugen Pillow und die TTF-Dateien; beides wird nicht ausgeliefert).

Geplant (Phase 5): `knowledge_base.json` aus [PS5-Jailbreak-Doctor](https://github.com/asomimlrxd/PS5-Jailbreak-Doctor) (MIT, asomimlrxd) – bei Übernahme hier mit Namensnennung eintragen.

Nicht im Repo, nur zum Bauen benötigt: [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (GPLv3, John Törnblom) für `make ps5`. Der gebaute ELF linkt gegen dessen Laufzeit-Bibliotheken; vor einer Veröffentlichung die Lizenzfolgen prüfen.

Nicht im Repo, nur für `make ps5-ui` benötigt (Build-Schritte: `docs/PHASE3.md`):
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) (SDL2, zlib-Lizenz) – VideoOut/Pad-Anbindung. Statisch gelinkt (`libSDL2.a`).
- [libsamplerate](https://libsndfile.github.io/libsamplerate/) 0.2.2 (BSD-2-Clause, Erik de Castro Lopo) – Abhängigkeit von SDL2s Audiopfad, auch wenn PS5 Medic selbst kein Audio ausgibt.
