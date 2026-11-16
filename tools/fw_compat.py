#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prueft, ob die Funktionen, die der Dienst und die native App von PS5 Medic importieren, in den Systembibliotheken jeder Firmware vorhanden sind.

Gebraucht wird ein Ordner mit entschluesselten Systembibliotheken je Firmware (<Wurzel>/<XX.XX>/.../libkernel.sprx usw.) und der Parser aus
build_import_export_db.py (--parser; das Skript gehoert nicht zu diesem Projekt und wird nicht mitgeliefert). Es wird nur gelesen. Die Firmware-Dateien
gehoeren nie ins Repository.

Namen -> NID: Base64 (mit '-' statt '/') der ersten 8 Bytes von SHA-1(Name + Salz), Bytes umgekehrt. Als Massstab dient eine Firmware, auf der die App
nachweislich laeuft (--baseline, Standard 12.00): Was dort nicht aus den Bibliotheken aufloesbar ist (Lader, Compiler-Laufzeit), zaehlt nicht.

Aufruf (aus dem Projektordner):
  python3 tools/fw_compat.py --fw-root "C:/FW 1.00-12.70" --parser "C:/FW 1.00-12.70/build_import_export_db.py" \\
      --service build-ps5/ps5medicservice.elf --native-imports build-native/obj/imports.txt --native-libs libkernel libScePad libSceSystemService \\
      libSceUserService libSceVideoOut libSceLibcInternal [--min 4.00] [--max 14.10]

Rueckgabe: 0 = in jeder lesbaren Firmware (--min bis --max) ist alles vorhanden; 1 = es fehlt etwas; 2 = Eingabefehler.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.util
import os
import re
import struct
import sys
from pathlib import Path

SALT = bytes.fromhex("518D64A635DED8C1E6B039B1C3E55230")


def nid(name: str) -> str:
    """NID einer Funktion (an sceKernelUsleep = 1jfXLRVzisc und scePadReadState = YndgXqQVV7c geprueft)."""
    digest = hashlib.sha1(name.encode() + SALT).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip("=").replace("/", "-")


def fw_key(version: str) -> tuple[int, ...]:
    return tuple(int(x) for x in version.split("."))


def elf_imports(path: Path) -> tuple[list[str], list[str]]:
    """(importierte Namen, NEEDED-Bibliotheken ohne Endung) eines ELF64 mit Abschnittskoepfen (.dynsym/.dynstr)."""
    data = path.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise ValueError("kein ELF64: %s" % path)
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    if shoff == 0 or shnum == 0:
        raise ValueError("keine Abschnittskoepfe (Namen dann mit --native-imports angeben): %s" % path)
    sections = []
    for i in range(shnum):
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize)
        sections.append((name, typ, off, size, link, entsize))
    shstr = sections[shstrndx]

    def sec_name(n: int) -> str:
        start = shstr[2] + n
        return data[start:data.index(b"\0", start)].decode()

    by_name = {sec_name(s[0]): s for s in sections}
    dynsym, dynstr = by_name[".dynsym"], by_name[".dynstr"]
    names = []
    for k in range(dynsym[3] // 24):
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", data, dynsym[2] + k * 24)
        if k and st_shndx == 0 and st_name:  # nicht definiert = Import
            start = dynstr[2] + st_name
            names.append(data[start:data.index(b"\0", start)].decode())
    needed = []
    dyn = by_name.get(".dynamic")
    if dyn:
        for k in range(dyn[3] // 16):
            tag, val = struct.unpack_from("<qQ", data, dyn[2] + k * 16)
            if tag == 1:
                start = dynstr[2] + val
                needed.append(data[start:data.index(b"\0", start)].decode().rsplit(".", 1)[0])
    return sorted(set(names)), needed


def load_parser(path: Path):
    spec = importlib.util.spec_from_file_location("fw_parser", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["fw_parser"] = module
    spec.loader.exec_module(module)
    return module


def read_exports(root: Path, parser, versions: list[str], libs: set[str]) -> tuple[dict, dict]:
    """exports[fw][lib] = {NID}; unreadable[fw] = [lib, ...] (Datei da, aber nicht als ELF lesbar)."""
    exports: dict[str, dict[str, set[str]]] = {}
    unreadable: dict[str, list[str]] = {}
    for version in versions:
        exports[version] = {}
        for folder, _, files in os.walk(root / version):
            for file in files:
                stem = file.rsplit(".", 1)[0]
                if file.endswith((".sprx", ".prx")) and stem in libs:
                    try:
                        lib = parser.parse_library(Path(folder) / file, {})
                        exports[version].setdefault(stem, set()).update(entry["nid"] for entry in lib["exports"])
                    except Exception:
                        unreadable.setdefault(version, []).append(stem)
    return exports, unreadable


def evaluate(names: list[str], libs: set[str], exports: dict, versions: list[str], baseline: str) -> dict[str, tuple[str, list[str]]]:
    """je Firmware: ("ok" | "fehlt" | "unlesbar", [fehlende Namen]). Nur Namen, die in der Basis-Firmware aufloesbar sind, zaehlen."""
    base_union = set().union(*[exports.get(baseline, {}).get(lib, set()) for lib in libs])
    relevant = [n for n in names if nid(n) in base_union]
    result = {}
    for version in versions:
        have = [lib for lib in libs if lib in exports[version]]
        if not have:
            result[version] = ("unlesbar", [])
            continue
        union = set().union(*[exports[version][lib] for lib in have])
        missing = sorted(n for n in relevant if nid(n) not in union)
        result[version] = ("fehlt" if missing else "ok", missing)
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fw-root", type=Path, required=True, help="Ordner mit einem Unterordner je Firmware (XX.XX)")
    ap.add_argument("--parser", type=Path, required=True, help="build_import_export_db.py (liefert parse_library)")
    ap.add_argument("--service", type=Path, help="ELF des Dienstes (Importe und NEEDED werden gelesen)")
    ap.add_argument("--native-imports", type=Path, help="Textdatei mit den Importnamen der nativen App (title/build_native_ui.sh schreibt sie nach build-native/obj/imports.txt)")
    ap.add_argument("--native-libs", nargs="+", default=[], help="Bibliotheken der nativen App (ohne Endung)")
    ap.add_argument("--baseline", default="12.00", help="Firmware, auf der die App laeuft (Standard 12.00)")
    ap.add_argument("--min", dest="vmin", default="4.00", help="kleinste unterstuetzte Firmware (Standard 4.00)")
    ap.add_argument("--max", dest="vmax", default="14.10", help="groesste unterstuetzte Firmware (Standard 14.10)")
    args = ap.parse_args(argv)

    targets: list[tuple[str, list[str], set[str]]] = []
    try:
        if args.service:
            names, needed = elf_imports(args.service)
            targets.append(("Dienst", names, set(needed)))
        if args.native_imports:
            names = [l.strip() for l in args.native_imports.read_text().splitlines() if l.strip()]
            if not args.native_libs:
                raise ValueError("--native-libs fehlt")
            targets.append(("Native App", names, set(args.native_libs)))
    except (OSError, ValueError) as e:
        print("Eingabefehler:", e, file=sys.stderr)
        return 2
    if not targets or not args.fw_root.is_dir() or not args.parser.is_file():
        print("Eingabefehler: --service oder --native-imports, ein vorhandener --fw-root und --parser sind noetig", file=sys.stderr)
        return 2

    versions = sorted((d for d in os.listdir(args.fw_root) if re.fullmatch(r"\d+\.\d+", d)), key=fw_key)
    if args.baseline not in versions:
        print("Eingabefehler: Basis-Firmware %s nicht im Ordner" % args.baseline, file=sys.stderr)
        return 2
    parser = load_parser(args.parser)
    all_libs = set().union(*[t[2] for t in targets])
    exports, unreadable = read_exports(args.fw_root, parser, versions, all_libs)

    in_range = [v for v in versions if fw_key(args.vmin) <= fw_key(v) <= fw_key(args.vmax)]
    problems = 0
    for label, names, libs in targets:
        res = evaluate(names, libs, exports, versions, args.baseline)
        base_union = set().union(*[exports[args.baseline].get(l, set()) for l in libs])
        print("\n== %s: %d Importe, in %s aus den Bibliotheken aufloesbar: %d" % (label, len(names), args.baseline, sum(1 for n in names if nid(n) in base_union)))
        for v in versions:
            state, missing = res[v]
            mark = "" if v in in_range else "  (ausserhalb %s-%s)" % (args.vmin, args.vmax)
            detail = {"ok": "ok", "unlesbar": "Bibliotheken nicht lesbar (nicht entschluesselt?)", "fehlt": "FEHLT: " + ", ".join(missing[:6]) + (" ..." if len(missing) > 6 else "")}[state]
            print("  %-6s %s%s" % (v, detail, mark))
            if v in in_range and state == "fehlt":
                problems += 1
    covered_max = max(versions, key=fw_key)
    if fw_key(covered_max) < fw_key(args.vmax):
        print("\nHinweis: Der Ordner reicht nur bis %s; %s bis %s sind nicht geprueft." % (covered_max, covered_max, args.vmax))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
