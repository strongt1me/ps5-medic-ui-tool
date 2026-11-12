#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft die Uebersetzungen einer Sprache gegen die englische Quelle.

Aufruf:  python3 tools/l10n_check.py <fr|it|es> [ui|knowledge|errors|all]   (Standard: all)
 - ui:        ui/i18n/<sprache>.json hat genau die Schluessel von ui/i18n/en.json; Platzhalter {name} wie im Englischen
 - knowledge: knowledge/l10n/<sprache>.json (Regeln, Abdeckung, Messwerte, Selbsttests) deckt die Quelle ab (tools/l10n_extract.py)
 - errors:    knowledge/l10n/<sprache>.errors.json (Fehlercodes), Listen gleich lang
Ueberall: keine leeren Texte; Zahlen, Codes (CE-..., 0x...), Pfade, Titel-IDs und Produktnamen muessen im Text vorkommen, wenn sie im
Englischen vorkommen. Fehler werden mit Schluessel ausgegeben; Exit-Code 1 bei Fehlern.
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PLACEHOLDER = re.compile(r"\{[a-z_0-9]+\}")
# Zeichenfolgen, die unveraendert uebernommen werden muessen (Codes, Pfade, IDs, Dateinamen)
TOKENS = re.compile(r"CE-\d+-\d+|0x[0-9A-Fa-f]+|/[A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+|PPSA\d+|CUSA\d+|NPXS\d+|FAKE\d+|[A-Za-z0-9_\-]+\.(?:json|ini|elf|bin|log|txt|prx|sprx|exfat|ffpkg|db|sfo|self)")
errors = []


def err(where, msg):
    errors.append(f"{where}: {msg}")


def tokens(s):
    return set(TOKENS.findall(s)) | set(PLACEHOLDER.findall(s))


def check_text(where, en, got):
    if not isinstance(got, str) or not got.strip():
        err(where, "leer oder kein Text")
        return
    miss = tokens(en) - tokens(got)
    if miss:
        err(where, "fehlt im Text: " + ", ".join(sorted(miss)))


def check_value(where, en, got):
    if isinstance(en, list):
        if not isinstance(got, list) or len(got) != len(en):
            err(where, f"Liste mit {len(en)} Eintraegen erwartet")
            return
        for i, (a, b) in enumerate(zip(en, got)):
            check_text(f"{where}[{i}]", a, b)
    else:
        check_text(where, en, got)


def load(path):
    if not path.exists():
        err(str(path.relative_to(ROOT)), "Datei fehlt")
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as e:  # noqa: BLE001
        err(str(path.relative_to(ROOT)), f"kein gueltiges JSON: {e}")
        return None


def check_ui(lang):
    en = load(ROOT / "ui/i18n/en.json")
    got = load(ROOT / f"ui/i18n/{lang}.json")
    if en is None or got is None:
        return
    for k in sorted(set(en) - set(got)):
        err(f"ui.{k}", "Schluessel fehlt")
    for k in sorted(set(got) - set(en)):
        err(f"ui.{k}", "Schluessel gibt es im Englischen nicht")
    for k in en:
        if k in got:
            check_text(f"ui.{k}", en[k], got[k])


def check_overlay(lang, section_filter, filename):
    subprocess.run([sys.executable, str(ROOT / "tools/l10n_extract.py")], check=True, stdout=subprocess.DEVNULL)
    src = load(ROOT / "build/l10n_source.json")
    got = load(ROOT / "knowledge/l10n" / filename.format(lang=lang))
    if src is None or got is None:
        return
    for section in section_filter:
        if section not in got:
            err(section, "Abschnitt fehlt")
            continue
        for key in sorted(set(src[section]) - set(got[section])):
            err(f"{section}.{key}", "Eintrag fehlt")
        for key in sorted(set(got[section]) - set(src[section])):
            err(f"{section}.{key}", "Eintrag gibt es in der Quelle nicht")
        for key, fields in src[section].items():
            g = got[section].get(key)
            if g is None:
                continue
            for field, pair in fields.items():
                if field not in g:
                    err(f"{section}.{key}.{field}", "Feld fehlt")
                else:
                    check_value(f"{section}.{key}.{field}", pair["en"], g[field])


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("fr", "it", "es"):
        raise SystemExit(__doc__)
    lang = sys.argv[1]
    what = sys.argv[2] if len(sys.argv) > 2 else "all"
    if what in ("ui", "all"):
        check_ui(lang)
    if what in ("knowledge", "all"):
        check_overlay(lang, ("rules", "coverage", "metrics", "selftests"), "{lang}.json")
    if what in ("errors", "all"):
        check_overlay(lang, ("error_codes",), "{lang}.errors.json")
    for e in errors[:80]:
        print("FEHLER", e)
    if len(errors) > 80:
        print(f"... und {len(errors) - 80} weitere")
    print(f"{lang}/{what}: {len(errors)} Fehler")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
