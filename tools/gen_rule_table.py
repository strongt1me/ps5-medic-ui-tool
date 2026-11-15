#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt docs/RULE_TABLE.md aus knowledge/rules.json (Uebersicht aller Regeln: Kennung, Stufe, Firmware, Reparatur, Nachweis, erster Satz).

Aufruf:  python3 tools/gen_rule_table.py          (schreibt die Datei)
         python3 tools/gen_rule_table.py --check  (Exitcode 1, wenn die Datei nicht zur rules.json passt)
Die Tabelle ist erzeugt, nicht von Hand zu pflegen.
"""
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "docs", "RULE_TABLE.md")


def first_sentence(text):
    m = re.match(r"(.+?[.!?])(\s|$)", text.strip(), re.S)
    s = (m.group(1) if m else text.strip()).replace("|", "/").replace("\n", " ")
    return s if len(s) <= 140 else s[:137].rstrip() + "..."


def build():
    d = json.load(open(os.path.join(REPO, "knowledge", "rules.json"), encoding="utf-8"))
    rules = d["rules"]
    lines = [
        "<!-- SPDX-License-Identifier: GPL-3.0-or-later -->",
        "# Regel-Uebersicht",
        "",
        f"Erzeugt von `tools/gen_rule_table.py` aus `knowledge/rules.json` (db_version {d['db_version']}, {len(rules)} Regeln). Nicht von Hand aendern.",
        "",
        "| Regel | Stufe | Firmware | Reparatur | Nachweis | Aussage |",
        "|---|---|---|---|---|---|",
    ]
    for r in sorted(rules, key=lambda r: r["id"]):
        lines.append("| `{}` | {} | {} | {} | {} | {} |".format(
            r["id"], r.get("severity", ""), r.get("applies_fw") or "alle", f"`{r['fix']}`" if r.get("fix") else "-", r.get("verified", ""), first_sentence(r["text"]["de"])))
    return "\n".join(lines) + "\n"


def main():
    text = build()
    if "--check" in sys.argv:
        cur = open(OUT, encoding="utf-8", newline="").read().replace("\r\n", "\n") if os.path.exists(OUT) else ""
        return 0 if cur == text else 1
    open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print("geschrieben:", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
