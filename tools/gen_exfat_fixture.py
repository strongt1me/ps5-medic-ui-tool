#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt die synthetischen Images der Fixtures g1_neg_inline_image, g1_pos_image_ebootnew (exFAT) und g1_neg_inline_ffpfs, g1_neg_inline_ffpfsc
(derselbe exFAT-Inhalt im PFS-Behaelter, unkomprimiert bzw. PFSC-komprimiert) unter tests/fixtures/.

Unabhaengig vom C++-Bauer build_exfat_stub (libmedic/src/exfat.cpp): Die eboot.bin liegt hier **zerstueckelt** in der FAT-Kette (ohne
NoFatChain), das Verzeichnis hat zwei Dateien und der Name steht in anderer Schreibung ("EBOOT.BIN"). So prueft der Test den Leser gegen einen
zweiten Aufbau. Nur erfundene Inhalte. Aufruf: python3 tools/gen_exfat_fixture.py
"""
import os
import shutil
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIX = os.path.join(ROOT, "tests", "fixtures")


def fake_eboot(sdk):
    b = bytearray(0x1200)
    b[0:4] = bytes.fromhex("4f153d1d")
    struct.pack_into("<H", b, 0x18, 2)
    struct.pack_into("<QQQQ", b, 0x20, 0x10000, 0x1100, 0x20, 0x20)
    struct.pack_into("<QQQQ", b, 0x40, 0, 0x1000, 0x100, 0x100)
    elf = 0x60
    b[elf:elf + 4] = b"\x7fELF"
    struct.pack_into("<Q", b, elf + 32, 64)
    struct.pack_into("<H", b, elf + 54, 56)
    struct.pack_into("<H", b, elf + 56, 2)
    ph = elf + 64
    struct.pack_into("<I", b, ph, 1)
    struct.pack_into("<Q", b, ph + 8, 0x1000)
    struct.pack_into("<Q", b, ph + 32, 0x100)
    struct.pack_into("<I", b, ph + 56, 0x61000001)
    struct.pack_into("<Q", b, ph + 56 + 8, 0x1010)
    struct.pack_into("<Q", b, ph + 56 + 32, 0x40)
    struct.pack_into("<Q", b, 0x1010, 0x40)
    b[0x1018:0x101C] = b"ORBI"
    struct.pack_into("<I", b, 0x1010 + 0x14, sdk)
    return bytes(b)


def name_entries(name):
    raw = name.encode("utf-16le")
    out = b""
    for i in range(0, len(raw), 30):
        chunk = raw[i:i + 30].ljust(30, b"\0")
        out += b"\xC1\x00" + chunk
    return out


def file_entry_set(name, first_cluster, size, no_fat_chain):
    names = name_entries(name)
    n_names = len(names) // 32
    primary = bytearray(32)
    primary[0] = 0x85
    primary[1] = 1 + n_names
    struct.pack_into("<H", primary, 4, 0x20)
    stream = bytearray(32)
    stream[0] = 0xC0
    stream[1] = 0x03 if no_fat_chain else 0x01
    stream[3] = len(name)
    struct.pack_into("<Q", stream, 8, size)
    struct.pack_into("<I", stream, 20, first_cluster)
    struct.pack_into("<Q", stream, 24, size)
    return bytes(primary) + bytes(stream) + names


def build_image(eboot):
    bps_shift, spc_shift = 9, 1            # Sektor 512, Cluster 1024
    sector, csize = 512, 1024
    spc = 2
    n_eb = (len(eboot) + csize - 1) // csize   # 5 Cluster
    readme = b"Beispiel\n"
    # Cluster: 2 = Wurzelverzeichnis, 3 = readme.txt, danach die Stuecke der eboot.bin in bewusst gemischter Reihenfolge
    eb_clusters = [4 + i for i in range(n_eb)]
    order = list(eb_clusters)
    order = [order[2], order[0], order[3], order[1]] + order[4:]   # 6,4,7,5,8 (zerstueckelt)
    total_clusters = 3 + n_eb
    fat_sectors = ((total_clusters + 2) * 4 + sector - 1) // sector
    fat_off, heap_off = 24, 24 + fat_sectors
    total_sectors = heap_off + (total_clusters) * spc
    img = bytearray(total_sectors * sector)
    img[0:3] = b"\xEB\x76\x90"
    img[3:11] = b"EXFAT   "
    struct.pack_into("<Q", img, 72, total_sectors)
    struct.pack_into("<IIIII", img, 80, fat_off, fat_sectors, heap_off, total_clusters, 2)
    struct.pack_into("<H", img, 104, 0x0100)
    img[108] = bps_shift
    img[109] = spc_shift
    img[110] = 1
    img[510:512] = b"\x55\xAA"

    def fat(i, v):
        struct.pack_into("<I", img, fat_off * sector + 4 * i, v)

    fat(0, 0xFFFFFFF8)
    fat(1, 0xFFFFFFFF)
    fat(2, 0xFFFFFFFF)
    fat(3, 0xFFFFFFFF)
    for i, c in enumerate(order):
        fat(c, order[i + 1] if i + 1 < len(order) else 0xFFFFFFFF)
    # die Kette beginnt bei order[0]; die Cluster liegen in der Datei in Kettenreihenfolge
    def cluster_pos(c):
        return (heap_off + (c - 2) * spc) * sector

    root = file_entry_set("readme.txt", 3, len(readme), True) + file_entry_set("EBOOT.BIN", order[0], len(eboot), False)
    img[cluster_pos(2):cluster_pos(2) + len(root)] = root
    img[cluster_pos(3):cluster_pos(3) + len(readme)] = readme
    for i, c in enumerate(order):
        piece = eboot[i * csize:(i + 1) * csize]
        img[cluster_pos(c):cluster_pos(c) + len(piece)] = piece
    return bytes(img)


PFS_BS = 0x10000


def pfs_container(payload, compressed):
    """PFS-Behaelter (Version 2) mit genau einer Datei, dem exFAT-Image: Block 0 Kopf, Block 1 Inode-Tabelle (Inode 3 ist die Datei),
    ab Block 2 die Daten, bei compressed als PFSC (Tabelle bei 0x400, Bloecke zu 64 KiB roh oder zlib)."""
    head = bytearray(PFS_BS)
    struct.pack_into("<Q", head, 0, 2)
    struct.pack_into("<Q", head, 8, 20130315)
    struct.pack_into("<I", head, 0x20, PFS_BS)
    inodes = bytearray(PFS_BS)
    ino = 3 * 0xA8
    first_block = 2
    if not compressed:
        struct.pack_into("<I", inodes, ino + 0x04, 0)
        struct.pack_into("<Q", inodes, ino + 0x08, len(payload))
        struct.pack_into("<I", inodes, ino + 0x64, first_block)
        body = payload
    else:
        lbs = PFS_BS
        blocks = (len(payload) + lbs - 1) // lbs
        offs = [0x400 + (blocks + 1) * 8]
        data = []
        for i in range(blocks):
            chunk = payload[i * lbs:(i + 1) * lbs].ljust(lbs, b"\x00")
            z = zlib.compress(chunk, 9)
            blk = z if len(z) < lbs else chunk
            data.append(blk)
            offs.append(offs[-1] + len(blk))
        pfsc = bytearray(0x30)
        struct.pack_into("<I", pfsc, 0, 0x43534650)
        struct.pack_into("<I", pfsc, 8, 6)
        struct.pack_into("<I", pfsc, 0x0C, lbs)
        struct.pack_into("<Q", pfsc, 0x18, 0x400)
        struct.pack_into("<Q", pfsc, 0x28, len(payload))
        table = b"".join(struct.pack("<Q", o) for o in offs)
        body = bytes(pfsc).ljust(0x400, b"\x00") + table + b"".join(data)
        struct.pack_into("<I", inodes, ino + 0x04, 1)
        struct.pack_into("<Q", inodes, ino + 0x08, len(body))
        struct.pack_into("<I", inodes, ino + 0x64, first_block)
    return bytes(head) + bytes(inodes) + body


def make_pfs(name, ext, compressed, sdk):
    dst = os.path.join(FIX, name)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(FIX, "g1_neg_backport"), dst)
    shutil.rmtree(os.path.join(dst, "fs/mnt/usb0/homebrew/backports"))
    home = os.path.join(dst, "fs/mnt/usb0/homebrew")
    open(os.path.join(home, "PPSA00001" + ext), "wb").write(pfs_container(build_image(fake_eboot(sdk)), compressed))
    open(os.path.join(dst, "expect.json"), "w", encoding="utf-8", newline="\n").write('{"expect": [], "neg_for": ["game.needs_backport"]}\n')


def make(name, sdk, expect):
    dst = os.path.join(FIX, name)
    if os.path.exists(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(FIX, "g1_neg_backport"), dst)
    shutil.rmtree(os.path.join(dst, "fs/mnt/usb0/homebrew/backports"))
    home = os.path.join(dst, "fs/mnt/usb0/homebrew")
    open(os.path.join(home, "PPSA00001.exfat"), "wb").write(build_image(fake_eboot(sdk)))
    open(os.path.join(dst, "expect.json"), "w", encoding="utf-8", newline="\n").write(expect + "\n")


if __name__ == "__main__":
    make("g1_neg_inline_image", 0x12000040, '{"expect": ["smp.exfat_cluster"], "neg_for": ["game.needs_backport"]}')
    make("g1_pos_image_ebootnew", 0x13600040, '{"expect": ["game.needs_backport", "smp.exfat_cluster"]}')
    make_pfs("g1_neg_inline_ffpfs", ".ffpfs", False, 0x12000040)
    make_pfs("g1_neg_inline_ffpfsc", ".ffpfsc", True, 0x12000040)
    print("ok")
