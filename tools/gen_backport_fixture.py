#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt die synthetischen Fixtures zur Backport-Erkennung (collect_game_firmware) unter tests/fixtures/:

  g1_neg_inline_elf          eboot.bin als reines ELF (nicht signiert) mit SDK 12.00: gilt als eingebauter Backport
  g1_neg_inline_module       eboot.bin 12.00, Module 12.00 und 11.00 in sce_module: Backport vollstaendig
  g1_pos_backport_halb       eboot.bin 12.00, aber ein Modul mit SDK 13.60: halber Backport (game.backport_incomplete)
  g1_neg_marker              eboot.bin nicht lesbar (verschluesseltes Segment), fakelib/FW7: Marke deckt die Konsole (12.00)
  g1_pos_marker_eboot_hoch   Marke FW7, aber die eboot.bin traegt SDK 13.60: die Messung widerspricht der Marke, der Befund bleibt
  g1_pos_marker_zu_neu       Marke FW13 liegt ueber der Konsole (12.00): reicht nicht
  g1_neg_kraken_cache        .pkg-Spiel ueber Kraken Mount: der Cache /data/krakenmount/caches/<ID> (mit .done) traegt eboot.bin und Modul mit SDK 12.00
  g1_pos_kraken_cache_hoch   derselbe Cache, aber die eboot.bin traegt SDK 13.60: der Befund bleibt
  g1_pos_kraken_cache_halb   Cache mit eboot.bin 12.00, aber einem Modul mit 13.60: halber Backport
  g1_pos_kraken_cache_unfertig  Cache ohne .done (Kraken Mount ist noch nicht fertig): wird nicht gelesen, der Befund bleibt

Aufbau der Modulkoepfe wie in collectors.cpp (program_sdk_raw): fake-signierte SELF mit zwei Eintraegen (Pruefblock, Daten), ELF-Kopf, PT_LOAD und
das Param-Segment (PT_SCE_PROCPARAM mit "ORBI" bzw. PT_SCE_MODULE_PARAM mit BF F4 13 3C bei +8, SDK bei +0x14). Nur erfundene Inhalte.
Aufruf: python3 tools/gen_backport_fixture.py
"""
import os
import shutil
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")
SKELETON = os.path.join(FIX, "g1_neg_inline")  # procs.json, system.json und die zwei param.json (Titel PPSA00001, verlangt 13.60, Konsole 12.00)
FOLDER = "fs/mnt/usb0/homebrew/PPSA00001-Muster"

PROC, MODULE = 0x61000001, 0x61000002


def param_block(b, at, kind, sdk):
    struct.pack_into("<Q", b, at, 0x40)
    if kind == PROC:
        b[at + 8:at + 12] = b"ORBI"
    else:
        b[at + 8:at + 12] = bytes.fromhex("bff4133c")
    struct.pack_into("<I", b, at + 0x14, sdk)


def fake_self(sdk, kind=PROC, encrypted=False):
    """Fake-signierte SELF; `encrypted`: das Segment mit dem Modulkopf ist als verschluesselt gekennzeichnet (nicht lesbar)."""
    b = bytearray(0x1200)
    b[0:4] = bytes.fromhex("4f153d1d")
    struct.pack_into("<H", b, 0x18, 2)
    struct.pack_into("<QQQQ", b, 0x20, 0x10000, 0x1100, 0x20, 0x20)                    # Pruefblock des Segments 0
    struct.pack_into("<QQQQ", b, 0x40, 0x2 if encrypted else 0, 0x1000, 0x100, 0x100)  # Daten des Segments 0
    elf = 0x60
    b[elf:elf + 4] = b"\x7fELF"
    struct.pack_into("<Q", b, elf + 32, 64)
    struct.pack_into("<H", b, elf + 54, 56)
    struct.pack_into("<H", b, elf + 56, 2)
    ph = elf + 64
    struct.pack_into("<I", b, ph, 1)                       # PT_LOAD
    struct.pack_into("<Q", b, ph + 8, 0x1000)
    struct.pack_into("<Q", b, ph + 32, 0x100)
    struct.pack_into("<I", b, ph + 56, kind)               # Param-Segment
    struct.pack_into("<Q", b, ph + 56 + 8, 0x1010)
    struct.pack_into("<Q", b, ph + 56 + 32, 0x40)
    param_block(b, 0x1010, kind, sdk)
    return bytes(b)


def fake_elf(sdk, kind=PROC):
    """Reines ELF ohne SELF-Huelle: die Datei-Position des Modulkopfs steht im Programmkopf."""
    b = bytearray(0x1200)
    b[0:4] = b"\x7fELF"
    struct.pack_into("<Q", b, 32, 64)
    struct.pack_into("<H", b, 54, 56)
    struct.pack_into("<H", b, 56, 2)
    ph = 64
    struct.pack_into("<I", b, ph, 1)
    struct.pack_into("<Q", b, ph + 8, 0x1000)
    struct.pack_into("<Q", b, ph + 32, 0x100)
    struct.pack_into("<I", b, ph + 56, kind)
    struct.pack_into("<Q", b, ph + 56 + 8, 0x1010)
    struct.pack_into("<Q", b, ph + 56 + 32, 0x40)
    param_block(b, 0x1010, kind, sdk)
    return bytes(b)


def put(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def make(name, expect, neg_for, eboot, modules=(), marker=None):
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    for rel in ("procs.json", "system.json", "fs/user/app/PPSA00001/sce_sys/param.json", FOLDER + "/sce_sys/param.json"):
        os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(SKELETON, rel), os.path.join(dst, rel))
    put(os.path.join(dst, FOLDER, "eboot.bin"), eboot)
    for mod_name, data in modules:
        put(os.path.join(dst, FOLDER, "sce_module", mod_name), data)
    if marker:
        put(os.path.join(dst, FOLDER, "fakelib", marker), b"")
    parts = ['"expect": [%s]' % ", ".join('"%s"' % r for r in expect)]
    if neg_for:
        parts.append('"neg_for": [%s]' % ", ".join('"%s"' % r for r in neg_for))
    with open(os.path.join(dst, "expect.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write("{" + ", ".join(parts) + "}\n")


CACHE = "fs/data/krakenmount/caches/PPSA00001"


def make_cache(name, expect, neg_for, eboot, modules=(), done=True):
    """Spiel als .pkg ueber Kraken Mount: es gibt keinen Spielordner in einem Scanpfad, nur den Cache mit dem ausgepackten Inhalt."""
    dst = os.path.join(FIX, name)
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    for rel in ("procs.json", "system.json", "fs/user/app/PPSA00001/sce_sys/param.json"):
        os.makedirs(os.path.dirname(os.path.join(dst, rel)), exist_ok=True)
        shutil.copyfile(os.path.join(SKELETON, rel), os.path.join(dst, rel))
    put(os.path.join(dst, CACHE, "eboot.bin"), eboot)
    for mod_name, data in modules:
        put(os.path.join(dst, CACHE, "sce_module", mod_name), data)
    if done:
        put(os.path.join(dst, CACHE, ".done"), b'{"backport":null,"fakelib":null,"kind":"pkg","v":2}')
    parts = ['"expect": [%s]' % ", ".join('"%s"' % r for r in expect)]
    if neg_for:
        parts.append('"neg_for": [%s]' % ", ".join('"%s"' % r for r in neg_for))
    with open(os.path.join(dst, "expect.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write("{" + ", ".join(parts) + "}\n")


def main():
    sdk12, sdk11, sdk13 = 0x12000040, 0x11000040, 0x13600040
    needs = "game.needs_backport"
    half = "game.backport_incomplete"
    make("g1_neg_inline_elf", [], [needs, half], fake_elf(sdk12))
    make("g1_neg_inline_module", [], [needs, half], fake_self(sdk12),
         modules=[("libMuster1.sprx", fake_self(sdk12, MODULE)), ("libMuster2.prx", fake_self(sdk11, MODULE)), ("liesmich.txt", b"kein Programm\n")])
    make("g1_pos_backport_halb", [half], [needs], fake_self(sdk12),
         modules=[("libMuster1.sprx", fake_self(sdk13, MODULE)), ("libMuster2.prx", fake_self(sdk11, MODULE))])
    make("g1_neg_marker", [], [needs], fake_self(sdk12, encrypted=True), marker="FW7")
    make("g1_pos_marker_eboot_hoch", [needs], None, fake_self(sdk13), marker="FW7")
    make("g1_pos_marker_zu_neu", [needs], None, fake_self(sdk12, encrypted=True), marker="FW13")
    make_cache("g1_neg_kraken_cache", [], [needs, half], fake_self(sdk12), modules=[("libMuster.prx", fake_self(sdk12, MODULE))])
    make_cache("g1_pos_kraken_cache_hoch", [needs], None, fake_self(sdk13))
    make_cache("g1_pos_kraken_cache_halb", [half], None, fake_self(sdk12), modules=[("libMuster.prx", fake_self(sdk13, MODULE))])
    make_cache("g1_pos_kraken_cache_unfertig", [needs], None, fake_self(sdk12), done=False)
    print("ok")


if __name__ == "__main__":
    main()
