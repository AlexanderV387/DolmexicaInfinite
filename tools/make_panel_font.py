#!/usr/bin/env python3
# Makes panelfont.h: DejaVu Sans Bold's printable ASCII and Latin-1 (accents,
# ñ, ¿, ¡) as smooth (anti-aliased) alpha atlases at the pixel sizes the 3DS's touch panels draw, so
# the text is not scaled up from a pixel font.
# Usage: make_panel_font.py DejaVuSans-Bold.ttf panelfont.h
import sys
from PIL import Image, ImageDraw, ImageFont

SIZES = [(16, "Large"), (11, "Small")]
CODES = list(range(32, 127)) + list(range(160, 256))

def make(ttf, pixels):
    font = ImageFont.truetype(ttf, pixels)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    glyphs = []
    x, y, row_width = 1, 1, 256
    for code in CODES:
        ch = chr(code)
        advance = int(round(font.getlength(ch)))
        box = font.getbbox(ch)
        w = max(1, box[2] - box[0]) if code != 32 else 1
        if x + w + 1 > row_width:
            x, y = 1, y + height + 1
        glyphs.append((code, x, y, w, box[0] if code != 32 else 0, advance))
        x += w + 1
    atlas_h = y + height + 1
    image = Image.new("L", (row_width, atlas_h), 0)
    draw = ImageDraw.Draw(image)
    for code, gx, gy, w, xoff, advance in glyphs:
        if code == 32: continue
        draw.text((gx - xoff, gy), chr(code), font=font, fill=255)
    return image, glyphs, height, ascent

def main():
    ttf, out = sys.argv[1], sys.argv[2]
    lines = ["// Made by tools/make_panel_font.py from DejaVu Sans Bold (Bitstream Vera",
             "// license: free to use and redistribute). Alpha atlases of ASCII 32-126,",
             "// then Latin-1 160-255 (glyph 95 on).",
             "#pragma once", "#include <stdint.h>", "",
             "typedef struct { int16_t x, y, w, xOffset, advance; } PanelFontGlyph;", ""]
    for pixels, name in SIZES:
        image, glyphs, height, ascent = make(ttf, pixels)
        w, h = image.size
        lines.append(f"static const int gPanelFont{name}Width = {w}, gPanelFont{name}Height = {h}, gPanelFont{name}LineHeight = {height}, gPanelFont{name}Ascent = {ascent};")
        lines.append(f"static const PanelFontGlyph gPanelFont{name}Glyphs[{len(glyphs)}] = {{")
        lines.append("\t" + ", ".join(f"{{{gx}, {gy}, {gw}, {xo}, {adv}}}" for _, gx, gy, gw, xo, adv in glyphs))
        lines.append("};")
        data = image.tobytes()
        lines.append(f"static const uint8_t gPanelFont{name}Alpha[{len(data)}] = {{")
        for i in range(0, len(data), 32):
            lines.append("\t" + ",".join(str(b) for b in data[i:i + 32]) + ",")
        lines.append("};")
        lines.append("")
    open(out, "w").write("\n".join(lines) + "\n")

main()
