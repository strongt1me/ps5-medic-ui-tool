#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bettet ui/i18n/de.json und en.json als Rohstring in ui/src/i18n_embedded.cpp ein.

Die JSON-Dateien bleiben die Quelle. Ein Test vergleicht die eingebetteten Texte mit den Dateien,
damit ein vergessener Lauf auffaellt. Aufruf:  python3 tools/embed_i18n.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    out = ["// SPDX-License-Identifier: GPL-3.0-or-later",
           "// GENERIERT von tools/embed_i18n.py aus ui/i18n/*.json -- nicht von Hand aendern.",
           '#include "medic_ui/i18n.hpp"', "", "namespace medic::ui {", ""]
    for lang, name in (("de", "kDeJson"), ("en", "kEnJson")):
        text = (ROOT / "ui/i18n" / f"{lang}.json").read_text(encoding="utf-8")
        json.loads(text)  # muss gueltig sein
        if ')JSON"' in text:
            raise SystemExit("Textende-Marke im JSON")
        out.append(f'const char* const {name} = R"JSON({text})JSON";')
        out.append("")
    out.append("}  // namespace medic::ui")
    (ROOT / "ui/src/i18n_embedded.cpp").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
