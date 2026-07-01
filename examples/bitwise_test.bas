' bitwise operations — 32-bit unsigned model (docs/bitwise_design.md).
' band/bor/bxor/bnot/shl/shr/rotl/rotr. Large uint32 results are rendered lossily
' by string() (gBASIC's %g, ~6 sig figs) but the VALUE is exact, so those cases
' are checked by equality against an exact literal (prints true) rather than shown.
program demo(args)
    ' --- small results shown directly ---
    print("and " + string(band(12, 10)))
    print("or " + string(bor(12, 10)))
    print("xor " + string(bxor(12, 10)))
    print("shl " + string(shl(1, 8)))
    print("shr " + string(shr(256, 4)))
    print("rot0 " + string(rotl(123, 0)))

    ' --- large results verified by exact equality (value is exact) ---
    print("not0 " + string(bnot(0) = 4294967295))
    print("not_ff " + string(bnot(255) = 4294967040))
    print("involution " + string(bnot(bnot(305419896)) = 305419896))
    print("shl_wrap " + string(shl(2147483648, 1) = 0))
    print("rotl_msb " + string(rotl(2147483648, 1) = 1))
    print("rotr_lsb " + string(rotr(1, 1) = 2147483648))
    print("mask_ident " + string(band(bnot(0), 305419896) = 305419896))

    ' --- practical: pack / unpack an RGBA color (0x12345678) ---
    r = 18
    g = 52
    b = 86
    a = 120
    rgba = bor(bor(bor(shl(r, 24), shl(g, 16)), shl(b, 8)), a)
    print("pack " + string(rgba = 305419896))
    print("unpack " + string(band(shr(rgba, 24), 255)) + " " + string(band(shr(rgba, 16), 255)) + " " + string(band(shr(rgba, 8), 255)) + " " + string(band(rgba, 255)))

    ' --- domain guards raise (shown via error handling would exit); here just
    '     confirm normal in-range edge values work ---
    print("edge_max " + string(band(4294967295, 4294967295) = 4294967295))
    print("shr31 " + string(shr(2147483648, 31)))
end program
