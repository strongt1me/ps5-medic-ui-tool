#!/usr/bin/env python3
"""Erzeugt ui/src/font_data.cpp: vorgerenderte Glyphen (8-Bit-Alpha) aus DejaVu Sans.

DejaVu steht unter einer freizuegigen Lizenz (Bitstream Vera + Public Domain), siehe THIRD_PARTY.md.
Aufruf (WSL, Pillow noetig):  python3 tools/gen_font.py
Die erzeugte Datei wird eingecheckt; zur Laufzeit ist weder Pillow noch eine TTF-Datei noetig.
Zeichensatz: ASCII, Latin-1 und alle Zeichen aus knowledge/rules.json und ui/i18n/*.json.
"""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
DIR = pathlib.Path("/usr/share/fonts/truetype/dejavu")
# (Name, Datei, Pixelhoehe, Zeichensatz)
FONTS = [
    ("small", "DejaVuSans.ttf", 22, "full"),
    ("body", "DejaVuSans.ttf", 28, "full"),
    ("title", "DejaVuSans-Bold.ttf", 48, "basic"),
]


def wanted(kind):
    base = set(range(32, 127))
    extra = {ord(c) for c in "ÄÖÜäöüß…–„“”‘’·"}
    if kind == "basic":
        return sorted(base | extra)
    used = set()
    for p in [ROOT / "knowledge/rules.json", ROOT / "ui/i18n/de.json", ROOT / "ui/i18n/en.json"]:
        used |= {ord(c) for c in p.read_text(encoding="utf-8")}
    used = {c for c in used if c >= 32 and c != 127 and not (0x80 <= c < 0xA0)}
    return sorted(base | set(range(160, 256)) | extra | used)


def render(font, cp):
    ch = chr(cp)
    adv = int(round(font.getlength(ch)))
    size = font.size * 3
    img = Image.new("L", (size, size), 0)
    ox, oy = font.size, font.size * 2  # Ursprung auf der Grundlinie
    ImageDraw.Draw(img).text((ox, oy), ch, font=font, fill=255, anchor="ls")
    bbox = img.getbbox()
    if bbox is None:
        return dict(cp=cp, bx=0, by=0, w=0, h=0, adv=adv, data=b"")
    left, top, right, bottom = bbox
    return dict(cp=cp, bx=left - ox, by=oy - top, w=right - left, h=bottom - top, adv=adv,
                data=img.crop(bbox).tobytes())


def main():
    out = ["// GENERIERT von tools/gen_font.py aus DejaVu Sans -- nicht von Hand aendern.",
           '#include "font_data.hpp"', "", "namespace medic::ui::fontdata {", ""]
    for name, file, px, kind in FONTS:
        font = ImageFont.truetype(str(DIR / file), px)
        ascent, descent = font.getmetrics()
        glyphs, blob, off = [], bytearray(), 0
        for cp in wanted(kind):
            g = render(font, cp)
            g["off"] = off
            off += len(g["data"])
            blob += g["data"]
            glyphs.append(g)
        out.append(f"static const Rec k_{name}_recs[] = {{")
        for g in glyphs:
            out.append(f"    {{{g['cp']}, {g['bx']}, {g['by']}, {g['w']}, {g['h']}, {g['adv']}, {g['off']}}},")
        out.append("};")
        hexdata = blob.hex()
        out.append(f"static const char k_{name}_hex[] =")
        for i in range(0, len(hexdata), 120):
            out.append(f'    "{hexdata[i:i + 120]}"')
        if not hexdata:
            out.append('    ""')
        out[-1] += ";"
        out.append(f"const Blob {name}_blob = {{{ascent}, {ascent + descent}, k_{name}_recs, {len(glyphs)}, k_{name}_hex, {len(hexdata)}}};")
        out.append("")
        print(f"{name}: {len(glyphs)} Glyphen, {len(blob)} Bytes", file=sys.stderr)
    out.append("}  // namespace medic::ui::fontdata")
    (ROOT / "ui/src/font_data.cpp").write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
