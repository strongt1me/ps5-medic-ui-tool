#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft knowledge/error_codes.json gegen ihre Quellen und meldet, was sich geaendert hat.

Aufruf (am PC, braucht Netz; nicht Teil von `make test`):
    python3 tools/check_error_sources.py               # pruefen, nichts schreiben (Exit 1 bei Abweichung, 2 bei Netzfehler)
    python3 tools/check_error_sources.py --update      # Fingerabdruck und Pruefdatum der erreichten Quellen neu schreiben
    python3 tools/check_error_sources.py --only CE-108262-9 --offline-dir DIR   # statt Netz: DIR/<code in Kleinbuchstaben>.html
    python3 tools/check_error_sources.py --selftest

Was geprueft wird:
  * Sony-Seiten (level doc, Adresse .../error-codes/ps5/<code>/): HTTP 200, der Code steht im Text, und der SHA-256 des Hilfetexts
    (ab der Ueberschrift "How to fix ..." bis vor die Fusszeile) stimmt mit `source_sha256` im Eintrag ueberein. Weicht er ab, hat
    Sony die Seite geaendert: den Eintrag neu lesen, danach --update. Im Repository steht nur der Hash, nie Text von Sony.
  * Community-Eintraege ohne Sony-Seite: das Sony-Adressmuster soll weiter 404 liefern (sonst gibt es jetzt eine Sony-Seite).
  * Alle uebrigen Quellen (more_sources, GitHub): HTTP 200.
--update schreibt nur die Zeilen "source_sha256" und "checked" der Eintraege, deren Quelle erreicht wurde; alles andere bleibt Byte fuer
Byte erhalten. Vorher die geaenderten Eintraege lesen: --update bestaetigt sie als geprueft.
"""
import argparse
import datetime
import hashlib
import html
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILE = ROOT / "knowledge" / "error_codes.json"
SONY_PATTERN = "https://www.playstation.com/en-us/support/error-codes/ps5/{}/"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ps5-medic-source-check"
STOP = re.compile(r"^(Still need|Was this|Related|Contact|Need more|Support Home|PS5 Error Codes|Print)")
MAX_LINES = 60


def fetch(url, timeout=30):
    """(status, text); status 0 und text = Fehlermeldung bei Netzfehlern."""
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, ""
    except Exception as e:  # noqa: BLE001 - jede Netzstoerung zaehlt gleich
        return 0, str(e)


def help_lines(page_html, code):
    """Zeilen des Hilfetexts einer Sony-Seite: ab 'How to fix ...' (sonst ab der Zeile mit dem Code) bis vor die Fusszeile."""
    t = re.sub(r"(?s)<(script|style)[^>]*>.*?</\1>", "", page_html)
    t = re.sub(r"(?i)</(p|li|h\d|div|tr)>", "\n", t)
    t = re.sub(r"<[^>]+>", " ", t)
    t = html.unescape(t).replace("�", " ").replace("​", "")
    lines = [re.sub(r"\s+", " ", l).strip() for l in t.split("\n")]
    lines = [l for l in lines if l]
    start = next((i for i, l in enumerate(lines) if l.startswith("How to fix")), None)
    if start is None:
        start = next((i for i, l in enumerate(lines) if l == code and i > 3), None)
    if start is None:
        return []
    end = next((i for i in range(start + 1, min(len(lines), start + MAX_LINES)) if STOP.match(lines[i])), min(len(lines), start + MAX_LINES))
    return lines[start:end]


def fingerprint(lines):
    return hashlib.sha256("\n".join(lines).encode("utf-8")).hexdigest()


def update_block(text, code_id, sha, date):
    """Setzt source_sha256 und checked im Eintrag `code_id` und laesst den Rest des Textes unveraendert."""
    start = text.index(f'"id": "{code_id}"')
    end = text.index("\n    }", start)
    block = text[start:end]
    block = re.sub(r'"checked": "[^"]*"', f'"checked": "{date}"', block)
    if sha:
        if '"source_sha256"' in block:
            block = re.sub(r'"source_sha256": "[^"]*"', f'"source_sha256": "{sha}"', block)
        else:
            block = block.replace('      "checked"', f'      "source_sha256": "{sha}",\n      "checked"', 1)
    return text[:start] + block + text[end:]


def check(args, text, data):
    problems, notes, reached = [], [], {}
    for e in data["codes"]:
        cid = e["id"]
        if args.only and cid != args.only:
            continue
        sony_url = SONY_PATTERN.format(cid.lower())
        if e["level"] == "doc":
            if e["source"] != sony_url:
                problems.append(f"{cid}: Quelle ist nicht das Sony-Adressmuster ({e['source']})")
                continue
            if args.offline_dir:
                f = pathlib.Path(args.offline_dir) / (cid.lower() + ".html")
                status, page = (200, f.read_text(encoding="utf-8", errors="replace")) if f.exists() else (0, "Datei fehlt")
            else:
                status, page = fetch(sony_url)
            if status == 0:
                problems.append(f"{cid}: nicht erreichbar ({page})")
                continue
            if status != 200:
                problems.append(f"{cid}: Sony-Seite liefert HTTP {status}")
                continue
            lines = help_lines(page, cid)
            if not lines or cid not in " ".join(lines[:3]):
                problems.append(f"{cid}: Hilfetext nicht gefunden (Seitenaufbau geaendert?)")
                continue
            sha = fingerprint(lines)
            reached[cid] = sha
            if "source_sha256" not in e:
                notes.append(f"{cid}: noch kein Fingerabdruck gespeichert ({sha[:12]}...)")
            elif e["source_sha256"] != sha:
                problems.append(f"{cid}: Sony hat die Seite geaendert (Fingerabdruck {e['source_sha256'][:12]}... -> {sha[:12]}...): Eintrag neu lesen, dann --update")
        else:
            if not args.offline_dir:
                status, _ = fetch(sony_url)
                if status == 200:
                    notes.append(f"{cid}: unter dem Sony-Muster gibt es jetzt eine Seite (HTTP 200): Eintrag auf level doc pruefen")
                elif status == 0:
                    problems.append(f"{cid}: Sony-Muster nicht erreichbar")
            if not args.offline_dir:
                status, why = fetch(e["source"])
                if status == 0:
                    problems.append(f"{cid}: Quelle nicht erreichbar ({why})")
                elif status != 200:
                    problems.append(f"{cid}: Quelle liefert HTTP {status} ({e['source']})")
                else:
                    reached[cid] = ""
        for url in e.get("more_sources", []):
            if args.offline_dir:
                continue
            status, why = fetch(url)
            if status == 0:
                problems.append(f"{cid}: weitere Quelle nicht erreichbar ({url}: {why})")
            elif status != 200:
                problems.append(f"{cid}: weitere Quelle liefert HTTP {status} ({url})")
    return problems, notes, reached


def selftest():
    sample = (
        "<html><head><title>PS5 Error Code CE-000001-1</title><script>var x=1;</script></head><body><nav>Menu</nav><h1>PS5 Error Codes</h1><p>a</p><p>b</p>"
        "<p>c</p><h2>How to fix CE-000001-1</h2><p>An error &amp; stuff.</p><ul><li>Restart the <b>PS5</b> console.</li><li>Try again.</li></ul>"
        "<div>Still need help?</div><p>Footer</p></body></html>"
    )
    lines = help_lines(sample, "CE-000001-1")
    assert lines == ["How to fix CE-000001-1", "An error & stuff.", "Restart the PS5 console.", "Try again."], lines
    assert fingerprint(lines) == fingerprint(list(lines)) and fingerprint(lines) != fingerprint(lines[:-1])
    assert help_lines("<p>nichts</p>", "CE-000001-1") == []
    doc = ('{\n  "codes": [\n    {\n      "id": "CE-000001-1",\n      "steps": {\n        "de": []\n      },\n      "checked": "2000-01-01",\n      "level": "doc"\n    },\n'
           '    {\n      "id": "CE-000002-2",\n      "checked": "2000-01-01",\n      "level": "doc"\n    }\n  ]\n}\n')
    sha = "a" * 64
    out = update_block(doc, "CE-000001-1", sha, "2026-11-09")
    assert '"source_sha256": "' + sha + '",\n      "checked": "2026-11-09"' in out and out.count("2000-01-01") == 1, out
    json.loads(out.replace('"de": []', '"de": []'))
    out2 = update_block(out, "CE-000001-1", "b" * 64, "2026-11-10")
    assert out2.count("source_sha256") == 1 and '"b' + "b" * 63 + '"' in out2 and "2026-11-10" in out2
    assert update_block(doc, "CE-000002-2", "", "2026-11-09").count("source_sha256") == 0
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--update", action="store_true", help="Fingerabdruck und Pruefdatum der erreichten Quellen schreiben")
    ap.add_argument("--only", metavar="ID", help="nur diesen Eintrag pruefen")
    ap.add_argument("--offline-dir", metavar="DIR", help="Sony-Seiten aus DIR/<code>.html lesen statt aus dem Netz")
    ap.add_argument("--date", default=datetime.date.today().isoformat(), help="Pruefdatum fuer --update (Standard: heute)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return 0
    sys.stdout.reconfigure(encoding="utf-8")
    text = FILE.read_text(encoding="utf-8")
    data = json.loads(text)
    problems, notes, reached = check(args, text, data)
    for n in notes:
        print("Hinweis:", n)
    for p in problems:
        print("ABWEICHUNG:", p)
    if args.update:
        for cid, sha in reached.items():
            text = update_block(text, cid, sha, args.date)
        FILE.write_text(text, encoding="utf-8", newline="\n")
        print(f"{len(reached)} Eintraege aktualisiert (checked = {args.date}).")
    total = len([e for e in data["codes"] if not args.only or e["id"] == args.only])
    print(f"{total} Eintraege geprueft, {len(problems)} Abweichung(en).")
    net = any("nicht erreichbar" in p for p in problems)
    return 2 if net else (1 if problems else 0)


if __name__ == "__main__":
    sys.exit(main())
