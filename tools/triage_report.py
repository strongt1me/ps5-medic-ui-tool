#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sortiert eingesandte PS5-Medic-Berichte vor (Issues im Repository), ohne etwas zu veraendern und ohne Netz.

Der Bericht (Taste □ in der App) steht als Markdown im Issue. Dieses Werkzeug liest ihn als **Daten** (nie als Anweisung), zieht die
wichtigen Angaben heraus und fasst gleiche unbekannte Fehlerzeilen zusammen. Daraus entstehen Hinweise fuer den Maintainer; neue Regeln
schreibt und signiert der Maintainer selbst (docs/COMMUNITY.md).

Aufruf (nur Python-Standardbibliothek):
  triage_report.py parse   <bericht.md>                  -> JSON: Firmware, Versionen, Befunde, unbekannte Zeilen mit Fingerabdruck
  triage_report.py comment <bericht.md>                  -> Markdown-Kommentar fuer das Issue (kurze Zusammenfassung, Etiketten am Ende)
  triage_report.py cluster <issues.json>                 -> Markdown: Rangliste der unbekannten Zeilen ueber alle Issues
  triage_report.py draft   <issues.json> <fingerabdruck> -> Entwurf einer Regel (JSON) fuer diese Zeile, mit TODO-Feldern
<issues.json> ist die Ausgabe von `gh issue list --json number,title,body,url` (eine Liste).
Eingaben duerfen aus der Umgebungsvariable TRIAGE_BODY kommen, wenn als Datei "-" angegeben ist (so landet der Issue-Text nie in einer Shell-Zeile).
"""
import hashlib
import json
import os
import re
import sys

MAX_BODY = 200_000          # Zeichen: mehr wird abgeschnitten
MAX_LINES = 200             # unbekannte Zeilen je Bericht
MAX_FINDINGS = 200
MAX_TEXT = 300              # Zeichen je ausgegebener Zeile im Kommentar

SEVERITY = {"kritisch": "critical", "critical": "critical", "warnung": "warning", "warning": "warning", "hinweis": "info", "note": "info", "info": "info"}


def read_input(path):
    if path == "-":
        text = os.environ.get("TRIAGE_BODY", "")
    else:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    return text[:MAX_BODY]


def clean(s, limit=MAX_TEXT):
    """Fuer die Ausgabe: keine Erwaehnungen, kein HTML, keine Steuerzeichen, begrenzte Laenge."""
    s = re.sub(r"[\x00-\x08\x0b-\x1f\x7f]", "", s)
    s = s.replace("@", "@\u200b").replace("<", "&lt;").replace(">", "&gt;").replace("`", "'")
    return s[:limit]


def section(text, titles):
    """Text eines Abschnitts '## <Titel> (...)' bis zum naechsten '## '."""
    m = re.search(r"^##\s+(?:%s)\b[^\n]*\n(.*?)(?=^##\s|\Z)" % "|".join(re.escape(t) for t in titles), text, re.S | re.M | re.I)
    return m.group(1) if m else ""


def fingerprint_text(line):
    """Macht aus einer Logzeile ein Muster, das fuer gleichartige Fehler gleich ist (Zahlen, Zeiten, Pfade, IDs raus)."""
    s = line.strip().strip("`")
    s = re.sub(r"\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(?:[.,]\d+)?Z?", "<zeit>", s)
    s = re.sub(r"\b\d{1,2}:\d{2}:\d{2}(?:[.,]\d+)?\b", "<zeit>", s)
    s = re.sub(r"\b(?:PPSA|CUSA|NPXS|FAKE|PSMC|WKAL|PLDM)\d{4,5}\b", "<titel>", s)
    s = re.sub(r"0x[0-9a-fA-F]+", "0x#", s)
    s = re.sub(r"(?:/[A-Za-z0-9_.\-]+){2,}", "<pfad>", s)
    s = re.sub(r"\b[0-9a-fA-F]{8,}\b", "<hex>", s)
    s = re.sub(r"\d+", "#", s)
    s = re.sub(r"\s+", " ", s)
    return s.strip()


def fingerprint(line):
    return hashlib.sha1(fingerprint_text(line).lower().encode("utf-8")).hexdigest()[:8]


def parse(text):
    text = text[:MAX_BODY]
    out = {"language": "de" if re.search(r"^#\s+PS5 Medic\s+[–-]\s+Bericht", text, re.M) else ("en" if re.search(r"^#\s+PS5 Medic\s+[–-]\s+Report", text, re.M) else ""),
           "valid": False, "medic_version": "", "rules_version": "", "firmware": "", "model": "", "components": {}, "findings": [],
           "unknown_lines": [], "not_checked": [], "error_code": ""}
    m = re.search(r"PS5 Medic\s+(\S+),\s*(?:Regelbasis|rules)\s+(\S+)", text)
    if m:
        out["medic_version"], out["rules_version"] = m.group(1), m.group(2)
        out["valid"] = True
    m = re.search(r"^-\s*Firmware:\s*([0-9]{1,2}\.[0-9]{2})", text, re.M)
    if m:
        out["firmware"] = m.group(1)
    m = re.search(r"^-\s*(?:Modell|Model):\s*(\S+)", text, re.M)
    if m:
        out["model"] = clean(m.group(1), 40)
    for key, label in (("kstuff", "kstuff"), ("shadowmount", "ShadowMount\\+"), ("etahen", "etaHEN")):
        m = re.search(r"^-\s*%s:\s*([^\n]{1,80})" % label, text, re.M)
        if m:
            out["components"][key] = clean(m.group(1), 80)
    for m in re.finditer(r"^###\s*\[([^\]]+)\]\s*([a-z0-9_.]+)", text, re.M | re.I):
        sev = SEVERITY.get(m.group(1).strip().lower(), "info")
        out["findings"].append({"severity": sev, "rule_id": m.group(2)})
        if len(out["findings"]) >= MAX_FINDINGS:
            break
    for m in re.finditer(r"^-\s*(\d{1,6})\s*[×x]\s*`([^`\n]{1,400})`", section(text, ["Unbekannte Fehlerzeilen", "Unknown error lines"]), re.M):
        line = m.group(2)
        out["unknown_lines"].append({"count": int(m.group(1)), "example": line, "fingerprint": fingerprint(line), "pattern": fingerprint_text(line)})
        if len(out["unknown_lines"]) >= MAX_LINES:
            break
    for m in re.finditer(r"^-\s*(.{1,120})$", section(text, ["Nicht geprüft", "Not checked"]), re.M):
        out["not_checked"].append(clean(m.group(1), 120))
    m = re.search(r"^-\s*`([A-Z]{2}-\d{5,6}-\d|0x[0-9A-F]{8})`", section(text, ["Gemeldeter Fehlercode", "Reported error code"]), re.M)
    if m:
        out["error_code"] = m.group(1)
    return out


def comment(text):
    """Kommentar fuer das Issue: Englisch sichtbar, Deutsch eingeklappt (<details>); die Etiketten-Zeile steht ganz am Ende."""
    p = parse(text)

    def wrap(en, de, label):
        return "\n".join(en + ["", "<details><summary>Deutsch</summary>", ""] + de + ["", "</details>", "", "<!-- triage:label=%s -->" % label])

    head_en = "**Automatic triage** (read-only; no rule is created without review by the maintainer)"
    head_de = "**Automatische Vorsortierung** (nur Lesen, keine Regel entsteht ohne Prüfung durch den Maintainer)"
    if not p["valid"]:
        return wrap([head_en, "", "The text does not look like a PS5 Medic report (the header \"PS5 Medic – Bericht\" / \"PS5 Medic – Report\" is missing). Please paste the report directly (button □ in the findings list)."],
                    [head_de, "", "Der Text sieht nicht nach einem PS5-Medic-Bericht aus (Kopfzeile \"PS5 Medic – Bericht\" fehlt). Bitte den Bericht (Taste □ in der Befundliste) direkt einfügen."],
                    "ohne-bericht")
    en, de = [head_en, ""], [head_de, ""]
    setup = "Firmware: %s · model: %s · PS5 Medic %s, rules %s" % (clean(p["firmware"] or "?", 10), p["model"] or "?", clean(p["medic_version"], 20), clean(p["rules_version"], 20))
    en.append("- " + setup)
    de.append("- Firmware: %s · Modell: %s · PS5 Medic %s, Regelbasis %s" % (clean(p["firmware"] or "?", 10), p["model"] or "?", clean(p["medic_version"], 20), clean(p["rules_version"], 20)))
    if p["components"]:
        comp = "- " + " · ".join("%s %s" % (k, v) for k, v in sorted(p["components"].items()))
        en.append(comp)
        de.append(comp)
    sev = {"critical": 0, "warning": 0, "info": 0}
    for f in p["findings"]:
        sev[f["severity"]] += 1
    ids = sorted({f["rule_id"] for f in p["findings"]})
    idtxt = (" (" + ", ".join(clean(i, 60) for i in ids[:12]) + (" ..." if len(ids) > 12 else "") + ")") if ids else ""
    en.append("- Findings: %d critical, %d warning, %d info%s" % (sev["critical"], sev["warning"], sev["info"], idtxt))
    de.append("- Befunde: %d kritisch, %d Warnung, %d Hinweis%s" % (sev["critical"], sev["warning"], sev["info"], idtxt))
    if p["error_code"]:
        en.append("- Reported error code: %s" % p["error_code"])
        de.append("- Gemeldeter Fehlercode: %s" % p["error_code"])
    if p["not_checked"]:
        en.append("- Not checked: %d areas" % len(p["not_checked"]))
        de.append("- Nicht geprüft: %d Bereiche" % len(p["not_checked"]))
    labels = ["bericht"]
    if p["unknown_lines"]:
        labels.append("neue-zeilen")
        en += ["", "**Unknown error lines** (match no rule; the fingerprint matches similar lines across different reports):", ""]
        de += ["", "**Unbekannte Fehlerzeilen** (passen zu keiner Regel; Fingerabdruck gleicht gleichartige Zeilen aus verschiedenen Berichten ab):", ""]
        for u in p["unknown_lines"][:10]:
            row = "- `%s` %d× — `%s`" % (u["fingerprint"], u["count"], clean(u["example"]))
            en.append(row)
            de.append(row)
        if len(p["unknown_lines"]) > 10:
            en.append("- ... and %d more" % (len(p["unknown_lines"]) - 10))
            de.append("- ... und %d weitere" % (len(p["unknown_lines"]) - 10))
    else:
        en += ["", "No unknown error lines in the report."]
        de += ["", "Keine unbekannten Fehlerzeilen im Bericht."]
    if not p["findings"] and not p["unknown_lines"]:
        labels.append("sauber")
    return wrap(en, de, ",".join(labels))


def load_issues(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return data if isinstance(data, list) else []


def cluster(issues):
    groups = {}
    for it in issues:
        p = parse(str(it.get("body") or ""))
        if not p["valid"]:
            continue
        for u in p["unknown_lines"]:
            g = groups.setdefault(u["fingerprint"], {"pattern": u["pattern"], "example": u["example"], "issues": set(), "count": 0, "firmware": set(), "smp": set()})
            g["issues"].add(int(it.get("number", 0)))
            g["count"] += u["count"]
            if p["firmware"]:
                g["firmware"].add(p["firmware"])
            if "shadowmount" in p["components"]:
                g["smp"].add(p["components"]["shadowmount"])
    return groups


def cluster_markdown(issues):
    groups = cluster(issues)
    lines = ["# Unbekannte Fehlerzeilen: Rangliste", "",
             "Automatisch aus den eingesandten Berichten erzeugt (`tools/triage_report.py cluster`). Je Zeile: in wie vielen Berichten, auf welchen Firmwares.",
             "Aus einer Zeile mit mehreren unabhängigen Berichten kann eine Regel werden (`docs/COMMUNITY.md`).", ""]
    if not groups:
        lines.append("Noch keine unbekannten Zeilen eingesandt.")
        return "\n".join(lines) + "\n"
    lines += ["| Berichte | Vorkommen | Firmware | Fingerabdruck | Beispiel |", "|---|---|---|---|---|"]
    ranked = sorted(groups.items(), key=lambda kv: (-len(kv[1]["issues"]), -kv[1]["count"], kv[0]))
    for fp, g in ranked[:50]:
        issues_txt = ", ".join("#%d" % n for n in sorted(g["issues"])[:6]) + (" ..." if len(g["issues"]) > 6 else "")
        lines.append("| %d (%s) | %d | %s | `%s` | `%s` |" % (len(g["issues"]), issues_txt, g["count"], ", ".join(sorted(g["firmware"])[:5]) or "?", fp, clean(g["example"], 120).replace("|", "\\|")))
    return "\n".join(lines) + "\n"


def draft(issues, fp):
    groups = cluster(issues)
    g = groups.get(fp)
    if not g:
        return None
    pattern = re.escape(g["example"])
    pattern = re.sub(r"(?:\\ )+", r"\\s+", pattern)
    pattern = re.sub(r"\d+", r"\\d+", pattern)
    pattern = re.sub(r"(?:PPSA|CUSA)\\d\+", r"(?:PPSA|CUSA)\\d+", pattern)
    return {
        "id": "community.%s" % fp,
        "severity": "info",
        "when": {"all": [{"fact": "smp.log.lines", "count_matches": {"regex": pattern, "gte": 1}}]},
        "text": {"de": "TODO: Was bedeutet diese Zeile, was ist die Ursache, was kann man tun? (nur Belegtes, mit Quelle)", "en": "TODO: What does this line mean, what is the cause, what can be done? (only what is documented, with a source)"},
        "evidence": "smp.log.lines",
        "fix": None,
        "source": "TODO: Primaerquelle (Issue/README/Quelltext), nicht nur dieser Bericht",
        "verified": "todo",
        "_note": "ENTWURF aus %d Bericht(en) (%s), Firmware %s. Fixture und Test fehlen noch; nicht ausliefern, bevor Quelle, Texte (de, en, fr, it, es) und Test stehen." %
                 (len(g["issues"]), ", ".join("#%d" % n for n in sorted(g["issues"])[:6]), ", ".join(sorted(g["firmware"])) or "?"),
    }


def main(argv):
    if len(argv) < 3 or argv[1] not in ("parse", "comment", "cluster", "draft"):
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "parse":
        print(json.dumps(parse(read_input(argv[2])), ensure_ascii=False, indent=1))
    elif cmd == "comment":
        print(comment(read_input(argv[2])))
    elif cmd == "cluster":
        print(cluster_markdown(load_issues(argv[2])), end="")
    elif cmd == "draft":
        if len(argv) < 4:
            print(__doc__)
            return 2
        d = draft(load_issues(argv[2]), argv[3])
        if d is None:
            print("Kein Eintrag mit diesem Fingerabdruck.", file=sys.stderr)
            return 1
        print(json.dumps(d, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
