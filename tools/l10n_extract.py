#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Zieht alle uebersetzbaren Wissenstexte (Deutsch und Englisch) in eine Quelldatei fuer Uebersetzer.

Die Wissensdatenbank (knowledge/rules.json, knowledge/error_codes.json) und die Selbsttests tragen ihre Texte nur auf Deutsch und
Englisch. Weitere Sprachen liegen als Ueberlagerung in knowledge/l10n/<sprache>.json (Regeln, Abdeckung, Messwerte, Selbsttests) und
knowledge/l10n/<sprache>.errors.json (Fehlercodes). Fehlt ein Eintrag, zeigt die Oberflaeche Englisch.

Aufruf:  python3 tools/l10n_extract.py [ausgabe.json]     (Standard: build/l10n_source.json)
Form der Quelle (je Wert {"de": ..., "en": ...}, bei Listen Listen):
  rules[<id>] = {"text", "confidence_reason"?}      coverage[<id>] = {"text"}     metrics[<id>] = {"text"}
  selftests[<id>] = {"name", "proves_not"}          error_codes[<id>] = {"title", "text", "steps", "notes"}
Die Ueberlagerung einer Sprache hat dieselbe Form, nur mit dem Text der Sprache statt {"de","en"} (Listen bleiben Listen).
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def pair(obj):
    return {"de": obj["de"], "en": obj["en"]}


def selftests():
    text = (ROOT / "libmedic/src/selftest.cpp").read_text(encoding="utf-8")
    out = {}
    # {{"id", TestTier::X, "risk", sekunden, "name_de", "name_en", "proves_not_de", "proves_not_en"}
    pat = re.compile(r'\{\{"([a-z0-9_.]+)",\s*TestTier::\w+,\s*"\w+",\s*\d+,\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}')
    for m in pat.finditer(text):
        sid, nd, ne, pd, pe = m.groups()
        un = lambda s: json.loads('"' + s + '"')
        out[sid] = {"name": {"de": un(nd), "en": un(ne)}, "proves_not": {"de": un(pd), "en": un(pe)}}
    return out


def main():
    dest = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/l10n_source.json"
    rules = json.loads((ROOT / "knowledge/rules.json").read_text(encoding="utf-8"))
    errs = json.loads((ROOT / "knowledge/error_codes.json").read_text(encoding="utf-8"))
    src = {"rules": {}, "coverage": {}, "metrics": {}, "selftests": selftests(), "error_codes": {}}
    for r in rules["rules"]:
        e = {"text": pair(r["text"])}
        if "confidence" in r and "reason" in r["confidence"]:
            e["confidence_reason"] = pair(r["confidence"]["reason"])
        src["rules"][r["id"]] = e
    for c in rules["coverage"]:
        src["coverage"][c["id"]] = {"text": pair(c["text"])}
    for m in rules["metrics"]:
        src["metrics"][m["id"]] = {"text": pair(m["text"])}
    for c in errs["codes"]:
        e = {"title": pair(c["title"]), "text": pair(c["text"])}
        for k in ("steps", "notes"):
            if k in c:
                e[k] = pair(c[k])
        src["error_codes"][c["id"]] = e
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(src, ensure_ascii=False, indent=1) + "\n", encoding="utf-8", newline="\n")
    print(f"{dest}: {len(src['rules'])} Regeln, {len(src['coverage'])} Abdeckung, {len(src['metrics'])} Messwerte, "
          f"{len(src['selftests'])} Selbsttests, {len(src['error_codes'])} Fehlercodes")


if __name__ == "__main__":
    main()
