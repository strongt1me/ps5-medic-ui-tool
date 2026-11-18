#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt den Katalog fuer den Payload Manager (itsPLK, "Custom Repository").

Aufruf (nach `make ps5`):
    python3 tools/gen_pm_catalog.py [--base-url URL] [--out ordner]

Kopiert ps5medicui.elf und ps5medic.elf aus build-ps5/ nach <out> und schreibt
<out>/ps5-medic.json mit SHA-256 (Payload Manager prueft ihn bei jedem Download).
Format: siehe CUSTOM_REPOSITORIES.md im Repo itsPLK/ps5-payload-manager.
Nach jedem neuen Build neu ausfuehren, sonst passt die Pruefsumme nicht mehr.
"""
import argparse, hashlib, json, re, shutil, sys
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BASE = "https://raw.githubusercontent.com/strongt1me/ps5-medic-ui-tool/main/payload-manager/"


def version():
    txt = (ROOT / "libmedic/include/medic/report.hpp").read_text(encoding="utf-8")
    m = re.search(r'kMedicVersion = "([^"]+)"', txt)
    if not m:
        sys.exit("kMedicVersion nicht gefunden")
    return m.group(1)


def sha256(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base-url", default=DEFAULT_BASE, help="Ordner-URL, unter der die Dateien liegen (https, mit Schraegstrich am Ende)")
    ap.add_argument("--out", default=str(ROOT / "payload-manager"))
    ap.add_argument("--build", default=str(ROOT / "build-ps5"))
    a = ap.parse_args()
    base = a.base_url if a.base_url.endswith("/") else a.base_url + "/"
    out, build = Path(a.out), Path(a.build)
    out.mkdir(parents=True, exist_ok=True)
    ver, today = version(), date.today().isoformat()

    entries = [
        ("ps5medic", "PS5 Medic", "ps5medicui.elf",
         "Vollbild-App: prueft den Live-Zustand des Homebrew-Setups (Firmware, HEN, kstuff, ShadowMount+, Payloads, Dateien, "
         "Configs), erklaert Fehler in Klartext und repariert sicher mit Backup und Undo. Hinweis: Fuer den DualSense-Controller "
         "ueber websrv starten (Big-App-Lader); als reiner Payload laeuft die Bedienung nur ohne Pad."),
        ("ps5medic-diagnose", "PS5 Medic Diagnose", "ps5medic.elf",
         "Kleiner Diagnose-Payload ohne Oberflaeche: schreibt den Bericht nach /data/ps5medic/last_check.txt und legt einen "
         "Snapshot in /data/ps5medic/snapshots/ ab."),
    ]
    payloads = []
    for pid, name, fn, desc in entries:
        src = build / fn
        if not src.is_file():
            sys.exit(f"{src} fehlt -- zuerst `make ps5`")
        shutil.copyfile(src, out / fn)
        payloads.append({
            "id": pid, "name": name, "filename": fn, "url": base + fn,
            "version": ver, "last_update": today, "category": "Utilities",
            "description": desc, "checksum": sha256(src), "author": "strongt1me",
        })
    cat = {"name": "PS5 Medic", "payloads": payloads}
    (out / "ps5-medic.json").write_text(json.dumps(cat, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Fertig: {out / 'ps5-medic.json'} ({len(payloads)} Payloads, Version {ver})")
    print(f"Quelle im Payload Manager eintragen: {base}ps5-medic.json")


if __name__ == "__main__":
    main()
