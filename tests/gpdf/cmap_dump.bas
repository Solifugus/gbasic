' The glyph gpdf finds for every codepoint U+0000-U+2FFF and a sample past
' the BMP, one "codepoint glyph" line each, for tests/gpdf/check_cmap.py to
' hold against fontTools. args[0] = a font file.
program main(args)
    load gpdf
    f = gpdf.font_file(args[0])
    out = [ "format " + string(f.cmap.format) ]
    cps = []
    cp = 0
    while cp <= 0x2FFF
        append(cps, cp)
        cp = cp + 1
    end while
    for each c in [ 0xFFFF, 0x10000, 0x1D3FF, 0x1D400, 0x1D401, 0x1F600, 0x10FFFF ]
        append(cps, c)
    end for
    for each c in cps
        append(out, string(c) + " " + string(gpdf._glyph(f, c)))
    end for
    print join(out, chr(10))
end program
