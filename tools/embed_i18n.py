#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bettet die Texte aller Sprachen als Rohstring in ui/src/i18n_embedded.cpp ein: ui/i18n/<sprache>.json (Oberflaeche, de en fr it es) und fuer
fr, it, es zusaetzlich die Wissenstexte knowledge/l10n/<sprache>.json und <sprache>.errors.json (zu einem Dokument vereint).

Die JSON-Dateien bleiben die Quelle. Ein Test vergleicht die eingebetteten Texte mit den Dateien,
damit ein vergessener Lauf auffaellt. Aufruf:  python3 tools/embed_i18n.py
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent


def emit(out, name, text):
    # Rohstring-Literale sind auf 64 KiB je Stueck begrenzt (MSVC) und fuer andere Uebersetzer unhandlich: in Teile zu je 8000 Zeichen
    # zerlegen, an Zeilengrenzen oder sonst mitten im Text (die Teile werden aneinandergehaengt).
    if ')JSON"' in text:
        raise SystemExit("Textende-Marke im JSON")
    parts, cur = [], ""
    for line in text.splitlines(keepends=True):
        if len(cur) + len(line) > 8000 and cur:
            parts.append(cur)
            cur = ""
        while len(line) > 8000:
            parts.append(line[:8000])
            line = line[8000:]
        cur += line
    if cur:
        parts.append(cur)
    out.append(f"const char* const {name} =")
    out.extend(f'R"JSON({p})JSON"' for p in parts)
    out[-1] += ";"
    out.append("")


def main():
    out = ["// SPDX-License-Identifier: GPL-3.0-or-later",
           "// GENERIERT von tools/embed_i18n.py aus ui/i18n/*.json -- nicht von Hand aendern.",
           '#include "medic_ui/i18n.hpp"', "", "namespace medic::ui {", ""]
    for lang, name in (("de", "kDeJson"), ("en", "kEnJson"), ("fr", "kFrJson"), ("it", "kItJson"), ("es", "kEsJson")):
        text = (ROOT / "ui/i18n" / f"{lang}.json").read_text(encoding="utf-8")
        json.loads(text)  # muss gueltig sein
        emit(out, name, text)
    for lang, name in (("fr", "kFrKnowledge"), ("it", "kItKnowledge"), ("es", "kEsKnowledge")):
        base = json.loads((ROOT / "knowledge/l10n" / f"{lang}.json").read_text(encoding="utf-8"))
        base.update(json.loads((ROOT / "knowledge/l10n" / f"{lang}.errors.json").read_text(encoding="utf-8")))
        emit(out, name, json.dumps(base, ensure_ascii=False, separators=(",", ":")))
    out.append("}  // namespace medic::ui")
    (ROOT / "ui/src/i18n_embedded.cpp").write_text("\n".join(out) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
