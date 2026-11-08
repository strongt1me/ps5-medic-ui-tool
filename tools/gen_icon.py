#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Erzeugt ps5/homebrew/sce_sys/icon0.png (512x512): dunkelblaue Kachel mit weissem Kreuz. Aufruf: python3 tools/gen_icon.py"""
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 512
img = Image.new("RGB", (S, S), (22, 48, 92))
d = ImageDraw.Draw(img)
d.rounded_rectangle((40, 40, S - 40, S - 40), radius=64, outline=(70, 130, 210), width=10)
arm, half = 70, 190
d.rounded_rectangle((S // 2 - arm, S // 2 - half, S // 2 + arm, S // 2 + half), radius=26, fill=(255, 255, 255))
d.rounded_rectangle((S // 2 - half, S // 2 - arm, S // 2 + half, S // 2 + arm), radius=26, fill=(255, 255, 255))
out = ROOT / "ps5/homebrew/sce_sys/icon0.png"
out.parent.mkdir(parents=True, exist_ok=True)
img.save(out, optimize=True)
print(out, out.stat().st_size, "Byte")
