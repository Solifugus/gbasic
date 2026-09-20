' `positions: true` -- an XML node can say where in the document it came from.
'
' OPT-IN, and the default is asserted FIRST: a parse that did not ask must be
' byte-identical to what it always was, because every consumer in this tree and
' outside it reads these records by field and a new key changes `keys()`.
'
' THE LOAD-BEARING CHECK IS THE CLAMP. libxml2 stores a node's line in a 16-BIT
' field unless XML_PARSE_BIG_LINES is set, so `xmlGetLineNo` returns 65535 for
' everything past that -- a PLAUSIBLE NUMBER rather than an error, which is the
' worst way a position can fail. A fixture that stopped at a few dozen lines
' would pass on a build with the flag removed, so this one generates a document
' past the boundary and checks a line on the far side of it.
load xml

src {file}= "examples/fixtures/xml/hard_cases.xml"
text = read(src)

print "default keys:   " + encode(keys(xml.parse(text)))
p = xml.parse(text, { positions: true })
print "with positions: " + encode(keys(p))

print "root line " + string(p.line)
' `empty` is on line 8 and line 7 is a COMMENT -- so this is a real per-element
' position and not a running count of elements seen.
print "empty line " + string(xml.find(p, "empty").line)
' Two elements on ONE line must report the SAME line, which a counter cannot do.
d = xml.find_all(p, "dup")
print "dup lines " + string(d[0].line) + " " + string(d[1].line)

print "--- keep_space, both spellings ---"
print "boolean " + string(len(xml.parse("<p> <x/> </p>", true).children))
print "record  " + string(len(xml.parse("<p> <x/> </p>", { keep_space: true }).children))
print "default " + string(len(xml.parse("<p> <x/> </p>").children))

print "--- past the 16-bit clamp ---"
big = "<?xml version=\"1.0\"?>" + chr(10) + "<root>" + chr(10)
i = 0
while i < 70000
  big = big + "  <r/>" + chr(10)
  i = i + 1
end while
big = big + "</root>" + chr(10)
b = xml.parse(big, { positions: true })
last = b.children[len(b.children) - 1]
print "elements " + string(len(b.children))
print "last line " + string(last.line) + " (clamped would be 65535)"

print "--- an unknown option is refused by name ---"
on error goto next
bad = xml.parse(text, { positionz: true })
if error then
  print "refused: " + error.message
  error.clear()
end if
