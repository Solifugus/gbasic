' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A screen drawn in cells (tests/run_term.sh, tier screen): text, a wide
' character, a combining mark, colours and attributes, a filled box and the
' cursor. Shown, then shown again unchanged, then one cell changed, so the
' driver can hold what each show wrote. A key between them paces it: the
' driver sends one after reading each frame.
load term

term.open()
term.clear()
term.put(1, 1, "hello, terminal")
term.put(1, 2, "red bold", { fg: "red", bold: true })
term.put(10, 2, "on blue", { fg: "#ffffff", bg: "blue" })
term.put(1, 3, "wide:" + "中" + "x")
term.put(1, 4, "e" + "\u{301}" + " combined")
term.fill(1, 5, 6, 2, { bg: "green" })
term.put(2, 5, "box")
term.put(78, 1, "edge-clipped")
term.cursor(3, 7)
term.show()
print to error "frame 1, width of 中x is " + string(term.width("中x")) + ", colours " + string(term.colors())
e = term.wait(10 seconds)
term.show()
print to error "frame 2"
e = term.wait(10 seconds)
term.put(1, 1, "J")
term.show()
print to error "frame 3"
e = term.wait(10 seconds)
term.close()
