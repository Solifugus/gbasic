' WHAT `xml.parse` DOES WITH THE AWKWARD SHAPES -- pinned before the parser is
' rewritten, so the rewrite has something to be identical to.
'
' WHY THIS EXISTS. The module's positive coverage was three files of ~35 lines
' and NONE of them carried a comment, mixed content, a namespaced attribute or
' trailing whitespace -- exactly where "walk libxml2's tree" and "build from SAX
' callbacks" diverge. A rewrite judged only by the existing goldens could drop
' every comment, strip a prefix differently, or change where whitespace
' survives, and every one of those produces a PLAUSIBLE DOCUMENT that the old
' tests still accept.
'
' So this is a CHARACTERIZATION test: it records what the shipped parser does,
' including the parts that are arguably wrong. Where a line below looks
' surprising, the surprise is the point -- it is a fact about today's behaviour,
' and a deliberate change to it should move this golden and be argued for in the
' commit, not slip through.
load xml

src {file}= "examples/fixtures/xml/hard_cases.xml"
d = xml.parse(read(src))

print "root: " + d.name + " qname=" + d.qname

' NOTE `xmlns:ns` KEEPS its prefix while `ns:qattr` LOSES it. That asymmetry is
' current behaviour, not a typo in this fixture.
print "attrs: " + encode(d.attrs)

' COMMENTS ARE DROPPED. A leading and an interior comment are both absent, so
' the count here is elements and text only -- the single most likely thing a
' callback-driven rewrite would start keeping by accident.
print "children: " + string(len(d.children))

for each c, i in d.children
  if type(c) = "record" then
    print string(i) + " elem " + c.name + " qname=" + c.qname + " text=[" + xml.text(c) + "]"
  else
    print string(i) + " text [" + string(c) + "]"
  end if
end for

print "--- text() ---"
' MIXED CONTENT is concatenated across children, tails included.
print "mixed:    [" + xml.text(xml.find(d, "mixed")) + "]"
' ENTITIES are expanded at parse time, numeric and named alike.
print "entities: [" + xml.text(xml.find(d, "entities")) + "]"
' CDATA is UNWRAPPED into ordinary text -- it does not survive as a section.
print "cdata:    [" + xml.text(xml.find(d, "cdata")) + "]"
' text() TRIMS, though the stored text node does not (see the encode below).
print "spaced:   [" + xml.text(xml.find(d, "spaced")) + "]"
print "utf8:     [" + xml.text(xml.find(d, "utf8")) + "]"
print "deep:     [" + xml.text(xml.find(d, "nested/a/b/c")) + "]"
print "empty:    [" + xml.text(xml.find(d, "empty")) + "]"
print "dups:     " + string(len(xml.find_all(d, "dup")))

print "--- encode ---"
' NOT A ROUND TRIP, deliberately shown: CDATA comes back escaped rather than as
' a section, and `spaced` keeps the whitespace text() trimmed. Both are facts
' about the encoder that only a printed comparison reveals.
print xml.encode(d)
