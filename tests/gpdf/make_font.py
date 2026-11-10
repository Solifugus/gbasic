#!/usr/bin/env python3
"""The fonts tests/run_gpdf_font.sh embeds, made per run by fontTools, so every
glyph, advance and composite the test expects is one this file put there.

    make_font.py DIR [SYSTEM_FONT]

writes into DIR:
  fixture.ttf       TrueType outlines. Every mapped character's glyph is a box
                    of its own width; 'ł' is a COMPOSITE of 'l' and a stroke the
                    cmap never reaches; 'ź' is a composite of 'z' and 'acute',
                    and 'acute' is ITSELF a composite (of 'acutebase'), so a
                    subset must follow components two levels down. U+1D400 is
                    past the BMP, so the cmap has a format 12 subtable. The last
                    glyphs ("1", "2", "3") share one advance, so numberOfHMetrics is less than
                    numGlyphs and their side bearings sit in the bare tail of
                    hmtx. fpgm, prep and cvt are present, to be carried whole.
  fixture.otf       the same characters with PostScript (CFF) outlines.
  restricted.ttf    fixture.ttf with OS/2 fsType 2 (restricted licence).
  bitmaponly.ttf    fixture.ttf with OS/2 fsType 0x0200 (bitmap embedding only).
  helvetica.ttf     fixture.ttf whose PostScript name is "Helvetica".
  pair.ttc          a collection: fixture.ttf, then a font named "FixtureTwo".
  bmp.ttf           fixture.ttf without U+1D400, so its only Unicode cmap is
                    format 4, some of whose segments go through idRangeOffset.
  system4.ttf       with SYSTEM_FONT given: that font with its format 12
                    subtables removed, so its format 4 is the one read.
"""
import array
import os
import struct
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable
from fontTools.ttLib.tables import ttProgram
from fontTools.ttLib.tables._g_l_y_f import GlyphComponent
from fontTools.ttLib.ttCollection import TTCollection

UPEM = 1000
# character -> (glyph name, advance). Widths differ so a width sum names the
# glyphs that made it.
CHARS = {
    " ": ("space", 250),
    "Z": ("Z", 610), "a": ("a", 520), "k": ("k", 530), "ł": ("lslash", 290),
    "d": ("d", 560), "M": ("M", 830), "e": ("e", 540), "c": ("c", 480),
    "h": ("h", 570), "n": ("n", 575), "i": ("i", 260), "z": ("z", 470),
    "y": ("y", 500), "Ł": ("Lslash", 600), "ó": ("oacute", 555), "ź": ("zacute", 470),
    "l": ("l", 270), "o": ("o", 555), "İ": ("Idotaccent", 300), "ş": ("scedilla", 450),
    "Д": ("uni0414", 690), "λ": ("lambda", 515), "€": ("Euro", 620),
    "\U0001D400": ("u1D400", 700),
}
COMPOSITES = {  # glyph -> [(component, dx, dy)]
    "lslash": [("l", 10, 0), ("stroke", 0, 0)],
    "zacute": [("z", 0, 0), ("acute", 120, 0)],
    "oacute": [("o", 0, 0), ("acute", 160, 0)],
    "acute": [("acutebase", 0, 560)],
}
# glyphs no character reaches
UNMAPPED = [("stroke", 290), ("acutebase", 200), ("acute", 200)]
# the last glyphs, for "1", "2" and "3", share one advance, so hmtx keeps
# only their side bearings
TAIL = {"1": ("tail1", 333), "2": ("tail2", 333), "3": ("tail3", 333)}


def box(width, height, inset):
    pen = TTGlyphPen(None)
    pen.moveTo((inset, 0))
    pen.lineTo((inset, height))
    pen.lineTo((width - inset, height))
    pen.lineTo((width - inset, 0))
    pen.closePath()
    return pen


def build(path, ps_name, fs_type=0, cff=False, family="Fixture", bmp_only=False):
    chars = dict(CHARS)
    if bmp_only:
        del chars["\U0001D400"]
    order = ([".notdef"] + [g for g, _ in CHARS.values()] + [g for g, _ in UNMAPPED] +
             [g for g, _ in TAIL.values()])
    advance = {".notdef": 500}
    for g, w in CHARS.values():
        advance[g] = w
    for g, w in UNMAPPED + list(TAIL.values()):
        advance[g] = w
    fb = FontBuilder(UPEM, isTTF=not cff)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({ord(ch): g for ch, (g, _) in list(chars.items()) + list(TAIL.items())})
    # A box whose height also varies by glyph, so two glyphs never share an
    # outline and a CID pointing at the wrong one cannot compare equal.
    heights = {g: 300 + 7 * i for i, g in enumerate(order)}
    if cff:
        strings = {}
        for g in order:
            pen = T2CharStringPen(advance[g], None)
            if g != "space":
                pen.moveTo((20, 0)); pen.lineTo((20, heights[g]))
                pen.lineTo((advance[g] - 20, heights[g])); pen.lineTo((advance[g] - 20, 0))
                pen.closePath()
            strings[g] = pen.getCharString()
        fb.setupCFF(ps_name, {"FullName": ps_name}, strings, {})
    else:
        glyphs = {}
        for g in order:
            if g == "space":
                glyphs[g] = TTGlyphPen(None).glyph()
            elif g in COMPOSITES:
                glyphs[g] = None
            else:
                glyphs[g] = box(advance[g], heights[g], 20 + (heights[g] % 5)).glyph()
        for g, parts in COMPOSITES.items():
            pen = TTGlyphPen(None)
            glyph = pen.glyph()
            glyph.numberOfContours = -1
            glyph.components = []
            for name, dx, dy in parts:
                c = GlyphComponent()
                c.glyphName, c.x, c.y, c.flags = name, dx, dy, 0x0004  # ROUND_XY_TO_GRID
                glyph.components.append(c)
            glyphs[g] = glyph
        fb.setupGlyf(glyphs)
    metrics = {}
    for g in order:
        lsb = 0 if g == "space" else 20
        if g.startswith("tail"):
            lsb = 11 * int(g[-1])  # distinct bearings in the bare tail of hmtx
        metrics[g] = (advance[g], lsb)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": family, "styleName": "Regular", "psName": ps_name})
    fb.setupOS2(sTypoAscender=800, usWinAscent=800, usWinDescent=200, fsType=fs_type,
                sCapHeight=700, usWeightClass=400)
    fb.setupPost()
    if not cff:
        fpgm = newTable("fpgm"); fpgm.program = ttProgram.Program()
        fpgm.program.fromAssembly(["PUSHB[ ]", "0", "FDEF[ ]", "ENDF[ ]"])
        prep = newTable("prep"); prep.program = ttProgram.Program()
        prep.program.fromAssembly(["PUSHB[ ]", "1", "POP[ ]"])
        cvt = newTable("cvt "); cvt.values = array.array("h", [0, 10, 20, 700, -200])
        fb.font["fpgm"], fb.font["prep"], fb.font["cvt "] = fpgm, prep, cvt
    fb.save(path)


def main():
    out = sys.argv[1]
    build(os.path.join(out, "fixture.ttf"), "FixtureSans")
    build(os.path.join(out, "fixture.otf"), "FixtureSansCFF", cff=True)
    build(os.path.join(out, "restricted.ttf"), "FixtureRestricted", fs_type=2)
    build(os.path.join(out, "bitmaponly.ttf"), "FixtureBitmap", fs_type=0x0200)
    build(os.path.join(out, "helvetica.ttf"), "Helvetica")
    build(os.path.join(out, "two.ttf"), "FixtureTwo", family="FixtureTwo")
    c = TTCollection()
    c.fonts = [TTFont(os.path.join(out, "fixture.ttf")), TTFont(os.path.join(out, "two.ttf"))]
    c.save(os.path.join(out, "pair.ttc"))
    build(os.path.join(out, "bmp.ttf"), "FixtureBMP", bmp_only=True)
    if len(sys.argv) > 2:
        # Patched in the bytes, not re-saved: fontTools recompiles a cmap its
        # own way and drops idRangeOffset (measured, DejaVu Sans: 49 such
        # segments before, 0 after). Each record naming a format 12 subtable
        # is pointed at the format 4 one instead; nothing else moves.
        data = bytearray(open(sys.argv[2], "rb").read())
        ntables = struct.unpack(">H", data[4:6])[0]
        for i in range(ntables):
            tag, _, toff, _ = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
            if tag == b"cmap":
                cmap = toff
        recs = struct.unpack(">H", data[cmap + 2:cmap + 4])[0]
        offs = [struct.unpack(">I", data[cmap + 8 + 8 * i:cmap + 12 + 8 * i])[0] for i in range(recs)]
        fmts = [struct.unpack(">H", data[cmap + o:cmap + o + 2])[0] for o in offs]
        four = offs[fmts.index(4)]
        for i, f in enumerate(fmts):
            if f == 12:
                data[cmap + 8 + 8 * i:cmap + 12 + 8 * i] = struct.pack(">I", four)
        open(os.path.join(out, "system4.ttf"), "wb").write(bytes(data))

    # the premises, checked where they are made
    t = TTFont(os.path.join(out, "fixture.ttf"))
    nhm, n = t["hhea"].numberOfHMetrics, t["maxp"].numGlyphs
    if not nhm < n:
        sys.exit("make_font: numberOfHMetrics %d is not below numGlyphs %d" % (nhm, n))
    for name in ["bmp.ttf"] + (["system4.ttf"] if len(sys.argv) > 2 else []):
        formats, ranged = format4(os.path.join(out, name))
        if 12 in formats or 4 not in formats or ranged == 0:
            sys.exit("make_font: %s has cmap formats %s and %d idRangeOffset segments; "
                     "it must be format 4 only, with some" % (name, formats, ranged))


def format4(path):
    """The cmap formats in a file, and how many format 4 segments use
    idRangeOffset, read from the bytes rather than from fontTools' model."""
    raw = TTFont(path).reader["cmap"]  # the table's bytes as stored
    formats, ranged = set(), 0
    for i in range(struct.unpack(">H", raw[2:4])[0]):
        off = struct.unpack(">I", raw[8 + 8 * i:12 + 8 * i])[0]
        fmt = struct.unpack(">H", raw[off:off + 2])[0]
        formats.add(fmt)
        if fmt == 4:
            seg = struct.unpack(">H", raw[off + 6:off + 8])[0] // 2
            ro = struct.unpack(">%dH" % seg, raw[off + 16 + 6 * seg:off + 16 + 8 * seg])
            ranged = max(ranged, sum(1 for r in ro if r))
    return sorted(formats), ranged


if __name__ == "__main__":
    main()
