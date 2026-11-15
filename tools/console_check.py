#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft nach einem Test an der Konsole den Zustand von PS5 Medic, nur lesend, per FTP (Standard Port 2121).

Aufruf:  python3 tools/console_check.py <Adresse> [--port 2121]

Liest unter /data/ps5medic: rules.json (db_version gegen die lokale Datei), error_codes.json, last_facts.json (Alter, Anzahl Fakten),
journal.jsonl (offene Vorgaenge ohne commit), native.log (Fehlerzeilen) und listet die Sicherungen. Schreibt, loescht und benennt nichts um.
Nichts davon laeuft ueber das Internet; die Adresse ist die Konsole im Heimnetz.
"""
import argparse
import ftplib
import io
import json
import os
import sys
import time

ROOT = "/data/ps5medic"
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def fetch(ftp, path, limit=4 << 20):
    buf = io.BytesIO()
    try:
        ftp.retrbinary("RETR " + path, lambda b: buf.write(b) if buf.tell() < limit else None)
    except ftplib.all_errors:
        return None
    return buf.getvalue()


def listing(ftp, path):
    """Eintraege eines Ordners als (Name, Ist-Ordner). Der FTP-Server der Konsole kennt kein MLSD, deshalb NLST und CWD-Probe."""
    names = []
    try:
        ftp.retrlines("NLST " + path, names.append)
    except ftplib.all_errors:
        return []
    out = []
    for n in names:
        base = n.rsplit("/", 1)[-1]
        if base in (".", ".."):
            continue
        is_dir = False
        try:
            here = ftp.pwd()
            ftp.cwd(path.rstrip("/") + "/" + base)
            ftp.cwd(here)
            is_dir = True
        except ftplib.all_errors:
            pass
        out.append((base, is_dir))
    return out


def modified(ftp, path):
    try:
        return ftp.sendcmd("MDTM " + path)[4:].strip()
    except ftplib.all_errors:
        return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=2121)
    a = ap.parse_args()
    ftp = ftplib.FTP()
    ftp.connect(a.host, a.port, timeout=15)
    ftp.login()
    bad = 0

    def say(ok, text):
        nonlocal bad
        print(("OK    " if ok else "FEHLT ") + text)
        bad += 0 if ok else 1

    local = json.load(open(os.path.join(REPO, "knowledge", "rules.json"), encoding="utf-8"))
    raw = fetch(ftp, ROOT + "/knowledge/rules.json")
    if raw is None:
        say(True, "rules.json: keine eigene Kopie auf der Konsole (die eingebaute gilt)")
    else:
        try:
            ver = json.loads(raw.decode("utf-8")).get("db_version")
            say(ver == local.get("db_version"), f"rules.json auf der Konsole {ver}, lokal {local.get('db_version')}")
        except ValueError:
            say(False, "rules.json auf der Konsole ist kein gueltiges JSON")

    raw = fetch(ftp, ROOT + "/last_facts.json")
    if raw is None:
        say(False, "last_facts.json fehlt (noch kein Check-up seit dem letzten Start?)")
    else:
        try:
            facts = json.loads(raw.decode("utf-8"))
            mod = modified(ftp, ROOT + "/last_facts.json")
            say(True, f"last_facts.json: {len(facts)} Fakten, geaendert {mod}")
        except ValueError:
            say(False, "last_facts.json ist kein gueltiges JSON")

    raw = fetch(ftp, ROOT + "/journal.jsonl")
    if raw is None:
        say(True, "journal.jsonl: leer (keine Reparatur gelaufen)")
    else:
        open_txn = {}
        for line in raw.decode("utf-8", "replace").splitlines():
            try:
                j = json.loads(line)
            except ValueError:
                continue
            t = j.get("txn")
            if t is None:
                continue
            if j.get("type") == "intent":
                open_txn[t] = j
            elif j.get("type") in ("commit", "abort", "undo"):  # fertig, abgebrochen (zurueckgedreht) oder rueckgaengig gemacht
                open_txn.pop(t, None)
        say(not open_txn, f"journal.jsonl: {len(open_txn)} unfertige Vorgaenge" + (" " + ", ".join(map(str, open_txn)) if open_txn else ""))

    raw = fetch(ftp, ROOT + "/native.log")
    if raw is not None:
        lines = [l for l in raw.decode("utf-8", "replace").splitlines() if "error" in l.lower() or "fail" in l.lower()]
        say(not lines, f"native.log: {len(lines)} Fehlerzeilen" + (" (letzte: " + lines[-1][:120] + ")" if lines else ""))

    backups = [n for n, is_dir in listing(ftp, ROOT + "/backups") if is_dir]
    print(f"INFO  Sicherungen: {len(backups)}" + (f", neueste {sorted(backups)[-1]}" if backups else ""))
    ftp.quit()
    print("Fertig:", "alles in Ordnung" if not bad else f"{bad} Punkt(e) pruefen", time.strftime("(%d.%m.%Y %H:%M)"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
