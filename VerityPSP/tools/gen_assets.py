#!/usr/bin/env python3
"""Gera src/assets.c, src/assets.h e ICON0.PNG.

Uso:  python3 tools/gen_assets.py [caminho/da/fonte.ttf]

- Sprites: lê assets_src/*.png (mesmos PNGs do app Android) e grava como
  imagens indexadas (paleta ABGR8888, formato nativo do framebuffer do PSP).
- Fonte: rasteriza uma fonte monoespaçada (DejaVu Sans Mono por padrão) em
  bitmaps de alpha 8 bits, cobrindo Latin-1 (acentos do português) e algumas
  pontuações tipográficas que o Gemini costuma usar.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_SIZE = 11
DEFAULT_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"

# (nome do enum, arquivo png) — a ORDEM aqui define os índices SPR_*.
SPRITES = [
    ("IDLE", "verity_idle"),
    ("SERIOUS_IDLE", "verity_serious_idle"),
    ("SPIN_1", "verity_spin_001"),
    ("SPIN_2", "verity_spin_002"),
    ("SPIN_3", "verity_spin_003"),
    ("WINK_1", "verity_wink_001"),
    ("WINK_2", "verity_wink_002"),
    ("WINK_3", "verity_wink_003"),
    ("GLITCH_1", "verity_glitch_001"),
    ("GLITCH_2", "verity_glitch_002"),
    ("SPEAK_OPEN_1", "verity_speak_open_001"),
    ("SPEAK_OPEN_2", "verity_speak_open_002"),
    ("SPEAK_CLOSED_1", "verity_speak_closed_001"),
    ("SPEAK_CLOSED_2", "verity_speak_closed_002"),
    ("SERIOUS_SPEAK_1", "verity_serious_speak_001"),
    ("SERIOUS_SPEAK_2", "verity_serious_speak_002"),
]

EXTRA_CPS = [0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026, 0x2022, 0x20AC, 0x2192]


def fmt_array(values, per_line, fmt):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append("    " + ",".join(fmt % v for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def abgr(r, g, b, a):
    return (a << 24) | (b << 16) | (g << 8) | r


def gen_sprites():
    out = []
    for name, base in SPRITES:
        im = Image.open(os.path.join(ROOT, "assets_src", base + ".png")).convert("RGBA")
        w, h = im.size
        px = list(im.getdata())
        pal = []
        idx = []
        for (r, g, b, a) in px:
            c = abgr(r, g, b, a) if a else 0
            if c not in pal:
                pal.append(c)
            idx.append(pal.index(c))
        assert len(pal) <= 256, base
        out.append((name, w, h, pal, idx))
    return out


def gen_font(font_path):
    font = ImageFont.truetype(font_path, FONT_SIZE)
    asc, desc = font.getmetrics()
    cw = int(round(font.getlength("M")))
    ch = asc + desc
    cps = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + EXTRA_CPS
    cps = sorted(set(cps))
    data = []
    for cp in cps:
        img = Image.new("L", (cw, ch), 0)
        ImageDraw.Draw(img).text((0, 0), chr(cp), font=font, fill=255)
        data.extend(img.tobytes())
    return cw, ch, cps, data, asc


def main():
    font_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_FONT
    sprites = gen_sprites()
    cw, ch, cps, fdata, asc = gen_font(font_path)

    # ---- assets.h
    h = ["/* GERADO por tools/gen_assets.py — não edite à mão. */",
         "#ifndef ASSETS_H", "#define ASSETS_H", "",
         "#include <stdint.h>", "",
         "typedef struct {", "    int w, h;", "    const uint8_t *idx;   /* w*h índices na paleta */",
         "    const uint32_t *pal;  /* ABGR8888; entrada 0 pode ser transparente */", "} Sprite;", "",
         "enum {"]
    for name, *_ in sprites:
        h.append("    SPR_%s," % name)
    h += ["    SPR_COUNT", "};", "", "extern const Sprite SPRITES[SPR_COUNT];", "",
          "/* Fonte bitmap monoespaçada (alpha 8 bits por pixel). */",
          "#define FONT_CELL_W %d" % cw, "#define FONT_CELL_H %d" % ch,
          "#define FONT_NGLYPHS %d" % len(cps),
          "extern const uint16_t FONT_CPS[FONT_NGLYPHS];  /* codepoints, ordenados */",
          "extern const uint8_t FONT_DATA[FONT_NGLYPHS * FONT_CELL_W * FONT_CELL_H];", "",
          "#endif", ""]
    open(os.path.join(ROOT, "src", "assets.h"), "w").write("\n".join(h))

    # ---- assets.c
    c = ["/* GERADO por tools/gen_assets.py — não edite à mão. */",
         '#include "assets.h"', ""]
    for name, w, hh, pal, idx in sprites:
        c.append("static const uint32_t pal_%s[] = {\n%s\n};" % (name, fmt_array(pal, 6, "0x%08Xu")))
        c.append("static const uint8_t idx_%s[] = {\n%s\n};" % (name, fmt_array(idx, 32, "%d")))
    c.append("")
    c.append("const Sprite SPRITES[SPR_COUNT] = {")
    for name, w, hh, pal, idx in sprites:
        c.append("    [SPR_%s] = { %d, %d, idx_%s, pal_%s }," % (name, w, hh, name, name))
    c.append("};")
    c.append("")
    c.append("const uint16_t FONT_CPS[FONT_NGLYPHS] = {\n%s\n};" % fmt_array(cps, 12, "0x%04X"))
    c.append("")
    c.append("const uint8_t FONT_DATA[FONT_NGLYPHS * FONT_CELL_W * FONT_CELL_H] = {\n%s\n};"
             % fmt_array(fdata, 32, "%d"))
    c.append("")
    open(os.path.join(ROOT, "src", "assets.c"), "w").write("\n".join(c))

    # ---- ICON0.PNG (144x80)
    icon = Image.new("RGBA", (144, 80), (24, 24, 28, 255))
    spr = Image.open(os.path.join(ROOT, "assets_src", "verity_idle.png")).convert("RGBA")
    spr = spr.resize((spr.width, spr.height), Image.NEAREST)
    icon.alpha_composite(spr, ((144 - spr.width) // 2, (80 - spr.height) // 2))
    icon.save(os.path.join(ROOT, "ICON0.PNG"))

    print("OK: %d sprites, fonte %dx%d, %d glifos" % (len(sprites), cw, ch, len(cps)))


if __name__ == "__main__":
    main()
