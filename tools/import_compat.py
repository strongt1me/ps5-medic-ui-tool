#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Uebernimmt die Kompatibilitaetsliste (state.json aus dem Projekt ps5-zstd-kompatibilitaetsliste) als knowledge/compat.json
und als eingebettete Tabelle libmedic/src/compat_data.cpp. Es wandern nur Titel-ID, Ergebnis und Format; keine Namen von Testern, keine Notizen.

Aufruf:  python3 tools/import_compat.py <pfad/zu/state.json>
Ergebnis je Titel: ok (alle Berichte erfolgreich), fail (alle fehlgeschlagen), mixed (widerspruechlich). Unbestaetigte Eintraege entfallen.
"""
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FORMATS = {"ZSTD", "LZ4", "ZSTD/LZ4", "LZ4/ZSTD"}


def main():
    src = json.load(open(sys.argv[1], encoding="utf-8", errors="replace"))
    table = {}
    for g in src["games"]:
        if g.get("status") not in ("ok", "fail", "mixed"):
            continue
        fmt = g.get("format", "") if g.get("format", "") in FORMATS else ""
        for ppsa in re.findall(r"PPSA[0-9]{5}", g.get("ppsa", "")):
            table[ppsa] = (g["status"], fmt)
    out = {"schema_version": 1, "date": src["date"], "source": "https://github.com/strongt1me/ps5-zstd-kompatibilitaetsliste",
           "games": {k: {"status": v[0], "format": v[1]} for k, v in sorted(table.items())}}
    with open(os.path.join(REPO, "knowledge", "compat.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
        f.write("\n")
    lines = ["// SPDX-License-Identifier: GPL-3.0-or-later",
             "// Erzeugt von tools/import_compat.py aus knowledge/compat.json -- nicht von Hand aendern.",
             '#include "medic/compat.hpp"', "", "namespace medic {", "",
             f'const char* const kCompatDate = "{src["date"]}";',
             "const CompatEntry kCompatTable[] = {"]
    for k, v in sorted(table.items()):
        lines.append(f'    {{"{k}", "{v[0]}", "{v[1]}"}},')
    lines += ["};", f"const unsigned kCompatCount = {len(table)};", "", "}  // namespace medic", ""]
    with open(os.path.join(REPO, "libmedic", "src", "compat_data.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(len(table), "Titel,", src["date"])


if __name__ == "__main__":
    main()
