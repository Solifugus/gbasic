#!/usr/bin/env python3
"""Generate stdlib/gpdf_metrics.bas -- the core-14 character widths.

WHY THIS IS GENERATED AND COMMITTED, the same argument src/currency_table.h
makes: a PDF that names Helvetica does not embed it. The reader supplies its
own copy, so OUR widths must be the ones the reader assumes or the text we
measure and the text it draws disagree -- lines overflow their column and
nothing errors. Those widths therefore cannot depend on which fonts happen to
be installed on the machine that built gBASIC.

SOURCE. The URW base-35 AFMs shipped with ghostscript/poppler
(/usr/share/fonts/type1/urw-base35). They are the metric-compatible clones of
the Adobe core-14 -- being metrically identical is their entire purpose, and is
why every PDF reader substitutes them. Verified rather than assumed: spot
values match the published Adobe metrics exactly (Helvetica space 278, A 667,
W 944, a 556, i 222, m 833; Times-Roman space 250, A 722, a 444, m 778;
Courier uniformly 600).

What is extracted is a table of NUMBERS -- how wide each character is -- which
is a fact about the Adobe design published in the PDF specification itself, not
the outlines, which are the copyrightable part and are neither read nor shipped.

WINANSI is embedded below rather than read from a library, so this generator
needs nothing but the AFMs. It was cross-checked once against reportlab's
independent table on 2026-09-13: all 250 DEFINED positions identical.

The six that differ are the ones WinAnsi leaves undefined -- 127, 129, 141,
143, 144, 157 -- where reportlab follows Acrobat in mapping them to `bullet`
and this table leaves them unrepresentable. That is deliberate and is the
library's governing rule: an unrepresentable byte is REFUSED BY NAME rather
than drawn as some other character. Substituting a bullet for a byte the
author did not write is the silent-mangling failure this design exists to
avoid, and a future reader diffing against reportlab should know the
difference is a decision rather than a defect.
"""
import os, re, sys

AFM_DIR = "/usr/share/fonts/type1/urw-base35"

# PDF core font -> URW metric-compatible AFM
FONTS = [
    ("Helvetica",             "NimbusSans-Regular"),
    ("Helvetica-Bold",        "NimbusSans-Bold"),
    ("Helvetica-Oblique",     "NimbusSans-Italic"),
    ("Helvetica-BoldOblique", "NimbusSans-BoldItalic"),
    ("Times-Roman",           "NimbusRoman-Regular"),
    ("Times-Bold",            "NimbusRoman-Bold"),
    ("Times-Italic",          "NimbusRoman-Italic"),
    ("Times-BoldItalic",      "NimbusRoman-BoldItalic"),
    ("Courier",               "NimbusMonoPS-Regular"),
    ("Courier-Bold",          "NimbusMonoPS-Bold"),
    ("Courier-Oblique",       "NimbusMonoPS-Italic"),
    ("Courier-BoldOblique",   "NimbusMonoPS-BoldItalic"),
]

# WinAnsiEncoding (PDF spec, Annex D). Index is the byte; "" means the byte
# encodes no glyph and is therefore not representable in this encoding.
WINANSI = {
    128:"Euro",130:"quotesinglbase",131:"florin",132:"quotedblbase",133:"ellipsis",
    134:"dagger",135:"daggerdbl",136:"circumflex",137:"perthousand",138:"Scaron",
    139:"guilsinglleft",140:"OE",142:"Zcaron",145:"quoteleft",146:"quoteright",
    147:"quotedblleft",148:"quotedblright",149:"bullet",150:"endash",151:"emdash",
    152:"tilde",153:"trademark",154:"scaron",155:"guilsinglright",156:"oe",
    158:"zcaron",159:"Ydieresis",160:"space",161:"exclamdown",162:"cent",
    163:"sterling",164:"currency",165:"yen",166:"brokenbar",167:"section",
    168:"dieresis",169:"copyright",170:"ordfeminine",171:"guillemotleft",
    172:"logicalnot",173:"hyphen",174:"registered",175:"macron",176:"degree",
    177:"plusminus",178:"twosuperior",179:"threesuperior",180:"acute",181:"mu",
    182:"paragraph",183:"periodcentered",184:"cedilla",185:"onesuperior",
    186:"ordmasculine",187:"guillemotright",188:"onequarter",189:"onehalf",
    190:"threequarters",191:"questiondown",192:"Agrave",193:"Aacute",
    194:"Acircumflex",195:"Atilde",196:"Adieresis",197:"Aring",198:"AE",
    199:"Ccedilla",200:"Egrave",201:"Eacute",202:"Ecircumflex",203:"Edieresis",
    204:"Igrave",205:"Iacute",206:"Icircumflex",207:"Idieresis",208:"Eth",
    209:"Ntilde",210:"Ograve",211:"Oacute",212:"Ocircumflex",213:"Otilde",
    214:"Odieresis",215:"multiply",216:"Oslash",217:"Ugrave",218:"Uacute",
    219:"Ucircumflex",220:"Udieresis",221:"Yacute",222:"Thorn",223:"germandbls",
    224:"agrave",225:"aacute",226:"acircumflex",227:"atilde",228:"adieresis",
    229:"aring",230:"ae",231:"ccedilla",232:"egrave",233:"eacute",
    234:"ecircumflex",235:"edieresis",236:"igrave",237:"iacute",238:"icircumflex",
    239:"idieresis",240:"eth",241:"ntilde",242:"ograve",243:"oacute",
    244:"ocircumflex",245:"otilde",246:"odieresis",247:"divide",248:"oslash",
    249:"ugrave",250:"uacute",251:"ucircumflex",252:"udieresis",253:"yacute",
    254:"thorn",255:"ydieresis",
}
ASCII = {
    32:"space",33:"exclam",34:"quotedbl",35:"numbersign",36:"dollar",37:"percent",
    38:"ampersand",39:"quotesingle",40:"parenleft",41:"parenright",42:"asterisk",
    43:"plus",44:"comma",45:"hyphen",46:"period",47:"slash",48:"zero",49:"one",
    50:"two",51:"three",52:"four",53:"five",54:"six",55:"seven",56:"eight",
    57:"nine",58:"colon",59:"semicolon",60:"less",61:"equal",62:"greater",
    63:"question",64:"at",91:"bracketleft",92:"backslash",93:"bracketright",
    94:"asciicircum",95:"underscore",96:"grave",123:"braceleft",124:"bar",
    125:"braceright",126:"asciitilde",
}
for c in range(65, 91):  ASCII[c] = chr(c)
for c in range(97, 123): ASCII[c] = chr(c)

def encoding():
    enc = {}
    enc.update(ASCII)
    enc.update(WINANSI)
    return enc

def afm_widths(path):
    by_name = {}
    with open(path, "r", encoding="latin-1") as fh:
        for line in fh:
            m = re.match(r"^C\s+(-?\d+)\s*;\s*WX\s+(\d+)\s*;\s*N\s+(\S+)\s*;", line)
            if m:
                by_name[m.group(3)] = int(m.group(2))
    return by_name

def main():
    if not os.path.isdir(AFM_DIR):
        sys.exit("make_pdf_metrics: %s is not there; install the urw-base35 fonts" % AFM_DIR)
    enc = encoding()
    out = []
    out.append("' SPDX-License-Identifier: Apache-2.0")
    out.append("' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.")
    out.append("'")
    out.append("' GENERATED by tools/make_pdf_metrics.py -- do not edit by hand.")
    out.append("'")
    out.append("' The core-14 character widths, in 1/1000 of the font size, indexed by")
    out.append("' WinAnsi byte. A PDF naming Helvetica does not embed it: the reader uses")
    out.append("' its own copy, so these must be the widths the reader assumes or measured")
    out.append("' text and drawn text disagree and nothing errors. Committed rather than")
    out.append("' read at build time for the same reason src/currency_table.h is.")
    out.append("'")
    out.append("' A width of -1 means the byte encodes no glyph in WinAnsi. `gpdf` refuses")
    out.append("' such a byte by name rather than drawing something else.")
    out.append("")
    out.append("library gpdf_metrics")
    out.append("")
    out.append("    function fonts()")
    out.append("        return [ " + ", ".join('"%s"' % n for n, _ in FONTS) + " ]")
    out.append("    end function")
    out.append("")
    for name, afm in FONTS:
        w = afm_widths(os.path.join(AFM_DIR, afm + ".afm"))
        row = []
        for b in range(256):
            g = enc.get(b)
            row.append(str(w[g]) if (g and g in w) else "-1")
        out.append("    function _w_%s()" % name.replace("-", "_"))
        out.append("        return [ " + ", ".join(row[:64]) + ",")
        for start in (64, 128, 192):
            chunk = ", ".join(row[start:start+64])
            out.append("                 " + chunk + ("," if start < 192 else " ]"))
        out.append("    end function")
        out.append("")
    out.append("    ' The width table for one core font, or `unknown` for a name that is")
    out.append("    ' not one of the fourteen -- the caller decides what to do about it.")
    out.append("    function widths(font)")
    for name, _ in FONTS:
        out.append('        if font = "%s" then return _w_%s()' % (name, name.replace("-", "_")))
    out.append("        return unknown")
    out.append("    end function")
    out.append("")
    out.append("end library")
    out.append("")
    dest = "stdlib/gpdf_metrics.bas"
    with open(dest, "w") as fh:
        fh.write("\n".join(out))
    print("make_pdf_metrics: wrote %s (%d fonts)" % (dest, len(FONTS)))

main()
