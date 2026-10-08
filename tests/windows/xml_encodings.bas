' XML in a legacy character encoding, read into the right CODEPOINTS -- on
' Windows AND Linux.
'
' libxml2 decodes UTF-8, UTF-16 and ISO-8859-x itself and hands every OTHER
' declared encoding to iconv. On Linux that is glibc's; on Windows it is gBASIC's
' own (src/platform_win32.c, over the Windows code pages), because the LGPL
' libiconv must not be linked into a static gbasic.exe. Two implementations of
' one contract, so the expected answers here come from neither: each is the
' UTF-8 of characters the ENCODING STANDARD assigns those bytes (JIS X 0208,
' GB 2312, Big5, KS X 1001, KOI8-R, cp1252), written down from the tables.
'
' THE CHUNK TIER is the one an implementation gets wrong without raising
' anything: libxml2 converts in pieces, so a long document splits a two-byte
' character across calls, and a converter that does not hand the half character
' back (EINVAL) either drops it or decodes two garbage halves. 3000 characters
' in, exactly 3000 codepoints out, every one of them the right one.
'
'     gbasic tests/windows/xml_encodings.bas        (from the repository root)
'
' "mismatches: 0" is a pass.
load xml

tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
        tally.bad = tally.bad + 1
    end if
end function

' A document declaring `enc` whose only text is the given raw bytes.
function doc(enc, body)
    return "<?xml version=\"1.0\" encoding=\"" + enc + "\"?><r>" + from_bytes(body) + "</r>"
end function

function text_hex(enc, body)
    return lower(hex_encode(xml.text(xml.parse(doc(enc, body)))))
end function

print("-- each encoding, against its standard's own table --")
ok("Shift_JIS 93FA 967B is 日本",      text_hex("Shift_JIS", [147, 250, 150, 123]), "e697a5e69cac")
ok("windows-1252 80 is the euro sign", text_hex("windows-1252", [128]), "e282ac")
ok("windows-1252 9C is œ",             text_hex("windows-1252", [156]), "c593")
ok("KOI8-R F0 is П",                   text_hex("KOI8-R", [240]), "d09f")
ok("GBK D6D0 CEC4 is 中文",            text_hex("GBK", [214, 208, 206, 196]), "e4b8ade69687")
ok("Big5 A4A4 A4E5 is 中文",           text_hex("Big5", [164, 164, 164, 229]), "e4b8ade69687")
ok("EUC-KR C7D1 B1B9 is 한국",         text_hex("EUC-KR", [199, 209, 177, 185]), "ed959ceab5ad")
ok("ASCII stays ASCII through a legacy encoding", text_hex("Shift_JIS", [65, 66]), "4142")
ok("mixed one- and two-byte characters keep their order",
   text_hex("Shift_JIS", [65, 147, 250, 66]), "41e697a542")

print("-- a STREAMED document, so a character is split between conversion calls --")
' Through xml.reader, which reads a FILE in chunks. MEASURED that the reader is
' the path that matters: xml.parse converts a whole in-memory document in one
' call, so with it a converter that silently SWALLOWED a partial character
' still passed this tier (at 3000 and at 40000 characters alike). Each element
' is one single-byte character and fifty two-byte ones, so characters sit at
' both parities and ~200 KB guarantees chunk boundaries fall inside some.
body = []
for i = 1 to 50
    append(body, 147)
    append(body, 250)
end for
one = "<e>A" + from_bytes(body) + "</e>"
parts = ["<?xml version=\"1.0\" encoding=\"Shift_JIS\"?><r>"]
for i = 1 to 2000
    append(parts, one)
end for
append(parts, "</r>")
f {file}= "xml_encodings_tmp.xml"
write(f, join(parts, ""))
rd = xml.reader("xml_encodings_tmp.xml")
seen = 0
wrong = 0
while xml.skip_to(rd, "e")
    s = xml.text(xml.subtree(rd))
    seen = seen + 1
    if s != "A" + repeat("日", 50) then
        wrong = wrong + 1
    end if
end while
xml.close(rd)
delete(f)
ok("2000 streamed elements arrive", seen, 2000)
ok("and every one decodes to A plus fifty 日, wherever the chunks fell", wrong, 0)

print("-- what must be refused, not guessed --")
function refused(enc, body)
    on error goto next
    x = xml.parse(doc(enc, body))
    if error then
        error.clear()
        return true
    end if
    return false
end function
ok("a Shift_JIS lead byte with no valid trail byte is an error", refused("Shift_JIS", [147, 32]), true)
ok("an encoding nobody knows is an error", refused("X-NO-SUCH-ENCODING", [65]), true)
ok("CONTROL: the same helper accepts a valid document", refused("Shift_JIS", [147, 250]), false)

print("")
if tally.checks < 13 then
    print("BROKEN: only " + string(tally.checks) + " checks were counted")
    print("MISMATCHES: 1")
else
    print("checks: " + string(tally.checks))
    if tally.bad = 0 then
        print("mismatches: 0")
    else
        print("MISMATCHES: " + string(tally.bad))
    end if
end if
