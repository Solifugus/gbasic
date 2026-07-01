' Hexadecimal integer literals (0x..) and integer-aware number formatting.
' Hex literals parse to exact integer values; integer-valued numbers now print
' in full (no %g exponent), which pairs naturally with hex/bitwise work.
program demo(args)
    ' --- hex literals ---
    print(0xFF)
    print(0x10)
    print(0x00)
    print(0xDEADBEEF)
    print(0Xabcdef)
    print(0xff)

    ' hex in expressions / with bitwise ops
    print(0xFF00 + 0x00FF)
    print(band(0xFF00, 0x0FF0))
    print(bor(0xF0, 0x0F))
    print(shl(0x1, 4))

    ' --- integer formatting: large integers print in full, not 1e+06 ---
    print(1000000)
    print(4294967295)
    print(305419896)
    print(0 - 2000000000)

    ' non-integers are unchanged (compact form)
    print(3.14159)
    print(1.5)
    print(0.001)

    ' arrays of large integers render fully too
    print([1000000000, 2000000000, 255])
end program
