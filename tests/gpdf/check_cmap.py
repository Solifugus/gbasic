#!/usr/bin/env python3
"""Holds gpdf's character-to-glyph lookup (tests/gpdf/cmap_dump.bas's output)
to fontTools' reading of the same font's cmap, codepoint by codepoint.

    check_cmap.py FONT DUMP WANT_FORMAT

Every codepoint in the dump must map to fontTools' glyph id for it, or to 0
where fontTools has none. WANT_FORMAT is the subtable gpdf must have chosen
(12 when the font has one, else 4), so a run cannot pass on the wrong table.
"""
import sys

from fontTools.ttLib import TTFont

font_path, dump_path, want_format = sys.argv[1], sys.argv[2], sys.argv[3]
font = TTFont(font_path)
cmap = font.getBestCmap()
lines = open(dump_path).read().split("\n")
bad = 0
got_format = lines[0].split()[1]
if got_format != want_format:
    print("MISMATCH gpdf read cmap format %s, want %s" % (got_format, want_format))
    bad += 1
mapped = 0
n = 0
for line in lines[1:]:
    if not line:
        continue
    cp, gid = (int(x) for x in line.split())
    want = font.getGlyphID(cmap[cp]) if cp in cmap else 0
    n += 1
    mapped += want != 0
    if gid != want:
        bad += 1
        if bad <= 10:
            print("MISMATCH U+%04X: gpdf %d, fontTools %d" % (cp, gid, want))
print("codepoints: %d (mapped %d)\nmismatches: %d" % (n, mapped, bad))
sys.exit(1 if bad or mapped == 0 else 0)
