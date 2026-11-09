#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bettet knowledge/error_codes.json als Rohstring in ui/src/error_codes_embedded.cpp ein.

Die JSON-Datei bleibt die Quelle (Wissen steht nicht im C++-Code, siehe CLAUDE.md). Die Einbettung macht die UI zu einer
einzigen ELF-Datei ohne weitere Datei auf der Konsole. Ein Test vergleicht den eingebetteten Text mit der Datei, damit ein
vergessener Lauf auffaellt. Aufruf:  python3 tools/embed_error_codes.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    text = (ROOT / "knowledge" / "error_codes.json").read_text(encoding="utf-8")
    json.loads(text)  # muss gueltig sein
    if ')JSON"' in text:
        raise SystemExit("Textende-Marke im JSON")
    out = ["// SPDX-License-Identifier: GPL-3.0-or-later",
           "// GENERIERT von tools/embed_error_codes.py aus knowledge/error_codes.json -- nicht von Hand aendern.",
           '#include "medic_ui/error_data.hpp"', "", "namespace medic::ui {", "",
           f'const char* const kErrorCodesJson = R"JSON({text})JSON";', "", "}  // namespace medic::ui"]
    (ROOT / "ui" / "src" / "error_codes_embedded.cpp").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
