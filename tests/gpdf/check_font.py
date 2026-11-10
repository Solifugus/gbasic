#!/usr/bin/env python3
"""Holds an embedded font in a gpdf document to the ORIGINAL font file, with
fontTools reading both, so nothing gpdf computed is taken on trust.

    check_font.py PDF FONT LINES_JSON KEY

The PDF is first rewritten by mupdf (`mutool clean -d`, every stream
decompressed), so the objects read here are mupdf's serialization, not gpdf's.
The drawn strings come from LINES_JSON[KEY], the list the emitter was given.

For every glyph drawn, in order:
  - its CID's glyph in the subset has the SAME OUTLINE as the original font's
    glyph for that character, composites decomposed through the subset's own
    component numbers (so a component left out, or renumbered wrongly, shows);
  - its advance and left side bearing are the original's;
  - the CIDFont's /W width is advance * 1000 / unitsPerEm.
And for the font as a whole:
  - fontTools loads the subset with checksums enforced, and the whole file sums
    to 0xB1B0AFBA (head's checkSumAdjustment);
  - it holds exactly glyph 0, the drawn glyphs and their components, no more;
  - fpgm, prep and cvt are the original's bytes;
  - BaseFont and FontName are SIX CAPITALS + '+' + the PostScript name.
Prints `checks: N` and `mismatches: M`; exits nonzero on any mismatch.
"""
import io
import json
import os
import re
import struct
import subprocess
import sys
import tempfile

from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.ttLib import TTFont

checks = 0
bad = 0


def check(label, ok, detail=""):
    global checks, bad
    checks += 1
    if not ok:
        bad += 1
        print("MISMATCH %s %s" % (label, detail))


def outline(font, name):
    pen = DecomposingRecordingPen(font.getGlyphSet())
    font.getGlyphSet()[name].draw(pen)
    return pen.value


def closure(font, names):
    glyf = font["glyf"]
    out = set(names)
    todo = list(names)
    while todo:
        g = glyf[todo.pop()]
        if g.isComposite():
            for c in g.components:
                if c.glyphName not in out:
                    out.add(c.glyphName)
                    todo.append(c.glyphName)
    return out


def main():
    pdf, font_path, lines_path, key = sys.argv[1:5]
    lines = json.load(open(lines_path, encoding="utf-8"))[key]
    orig = TTFont(font_path, fontNumber=0)
    cmap = orig.getBestCmap()
    upem = orig["head"].unitsPerEm

    with tempfile.TemporaryDirectory() as tmp:
        clean = os.path.join(tmp, "clean.pdf")
        subprocess.run(["mutool", "clean", "-d", pdf, clean], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        data = open(clean, "rb").read()

    objs = {}
    for m in re.finditer(rb"(?m)^(\d+) 0 obj\s*(.*?)\s*endobj", data, re.S):
        objs[int(m.group(1))] = m.group(2)

    def dict_of(n):
        return objs[n].split(b"stream", 1)[0]

    def stream_of(n):
        body = objs[n]
        s = body.index(b"stream") + len(b"stream")
        if body[s:s + 2] == b"\r\n":
            s += 2
        elif body[s:s + 1] == b"\n":
            s += 1
        length = int(re.search(rb"/Length (\d+)", body).group(1))
        return body[s:s + length]

    type0 = [n for n in objs if b"/Subtype /Type0" in dict_of(n)]
    check("one Type0 font", len(type0) == 1, str(type0))
    t0 = dict_of(type0[0])
    base = re.search(rb"/BaseFont /(\S+)", t0).group(1).decode()
    ps = orig["name"].getDebugName(6)
    check("BaseFont is a subset tag and the PostScript name",
          re.fullmatch(r"[A-Z]{6}\+" + re.escape(ps), base) is not None, base)
    cid_n = int(re.search(rb"/DescendantFonts \[\s*(\d+) 0 R", t0).group(1))
    cidf = dict_of(cid_n)
    check("CIDFontType2, Identity", b"/CIDFontType2" in cidf and b"/CIDToGIDMap /Identity" in cidf)
    w = re.search(rb"/W \[\s*0 \[([^\]]*)\]", cidf).group(1).split()
    widths = [float(x) for x in w]
    fd = dict_of(int(re.search(rb"/FontDescriptor (\d+) 0 R", cidf).group(1)))
    check("FontName = BaseFont", ("/FontName /" + base).encode() in fd)
    prog = stream_of(int(re.search(rb"/FontFile2 (\d+) 0 R", fd).group(1)))

    sub_total = sum(struct.unpack(">%dI" % (len(prog) // 4), prog + b"\0" * (-len(prog) % 4)))
    check("the subset file sums to 0xB1B0AFBA", sub_total % 2**32 == 0xB1B0AFBA, hex(sub_total % 2**32))
    try:
        sub = TTFont(io.BytesIO(prog), checkChecksums=2)
        sub.ensureDecompiled()
        ok = True
    except Exception as e:  # a bad table checksum raises here
        ok = False
        print("fontTools: %s" % e)
    check("fontTools reads the subset, checksums enforced", ok)
    if not ok:
        print("checks: %d\nmismatches: %d" % (checks, bad))
        sys.exit(1)
    sorder = sub.getGlyphOrder()
    check("W has a width per subset glyph", len(widths) == len(sorder), "%d vs %d" % (len(widths), len(sorder)))

    # the drawn strings, in order, from the page streams of THIS font
    res_name = None
    drawn = []
    for n, body in sorted(objs.items()):
        d = body.split(b"stream", 1)[0]
        m = re.search(rb"/Font\s*<<([^>]*)>>", d)
        if m and re.search((r"/(F\d+) %d 0 R" % type0[0]).encode(), m.group(1)):
            res_name = re.search((r"/(F\d+) %d 0 R" % type0[0]).encode(), m.group(1)).group(1)
            content = stream_of(int(re.search(rb"/Contents (\d+) 0 R", d).group(1)))
            font_now = None
            for tok in re.finditer(rb"/(F\d+) [\d.]+ Tf|<([0-9A-Fa-f]*)> Tj", content):
                if tok.group(1):
                    font_now = tok.group(1)
                elif font_now == res_name:
                    drawn.append(tok.group(2).decode())
    check("as many embedded-font strings drawn as asked for, at least",
          len(drawn) >= len(lines), "%d vs %d" % (len(drawn), len(lines)))

    used = {".notdef"}
    for line, hexs in zip(lines, drawn):
        text = line["text"]
        cids = [int(hexs[i:i + 4], 16) for i in range(0, len(hexs), 4)]
        check("one CID per character: " + text, len(cids) == len(text), "%d vs %d" % (len(cids), len(text)))
        for ch, cid in zip(text, cids):
            want = cmap.get(ord(ch))
            if want is None or cid >= len(sorder):
                check("U+%04X has a glyph" % ord(ch), False, "cid %d" % cid)
                continue
            got = sorder[cid]
            used.add(want)
            check("U+%04X outline" % ord(ch), outline(sub, got) == outline(orig, want), "cid %d" % cid)
            check("U+%04X advance and bearing" % ord(ch), sub["hmtx"][got] == orig["hmtx"][want],
                  "%s vs %s" % (sub["hmtx"][got], orig["hmtx"][want]))
            check("U+%04X /W width" % ord(ch), abs(widths[cid] - orig["hmtx"][want][0] * 1000 / upem) < 0.006,
                  "%s vs %s" % (widths[cid], orig["hmtx"][want][0] * 1000 / upem))
    # every line asked for was drawn in this font (the extra ones are pages 2+)
    need = closure(orig, used)
    check("the subset holds exactly the drawn glyphs and their components",
          len(sorder) == len(need), "%d glyphs, %d needed" % (len(sorder), len(need)))
    check("glyph 0 is the original's missing glyph", outline(sub, sorder[0]) == outline(orig, ".notdef"))
    for tag in ("fpgm", "prep", "cvt "):
        if tag in orig:
            check(tag + " carried whole", tag in sub and sub.getTableData(tag) == orig.getTableData(tag))
    print("checks: %d\nmismatches: %d" % (checks, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
