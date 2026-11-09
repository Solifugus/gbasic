' WHERE xml REFUSES INSTEAD OF ANSWERING SOMETHING PLAUSIBLE (DOGFOOD rows 10
' and 12, 2026-11-08). Each refusal sits beside the neighbour that is accepted,
' so a refusal grown too wide is caught as surely as one gone missing.
'
' Row 10: an entity a DTD declares is never expanded in text (that is how XXE
' and billion-laughs attacks work), and the parser DROPPED the reference, so
' `<a>&y;</a>` read as an empty <a>. Now the parse, the reader and a subtree
' raise, naming the entity and its line.
' Row 12: xml.find answers unknown for a miss, and xml.text of unknown was "",
' which reads as an element with nothing in it. Now it raises.
load xml

on error goto next

' --- row 10: entities -----------------------------------------------------
d = xml.parse("<!DOCTYPE a [<!ENTITY y 'Y'>]><a>&y;</a>")
print "declared entity in text: " + error.message
error.clear()
d = xml.parse("<!DOCTYPE a [<!ENTITY y 'Y'>]>" + chr(10) + "<a>" + chr(10) + "<b>x &y; z</b></a>")
print "declared entity, line 3, between text: " + error.message
error.clear()
' the neighbours: the five predefined entities and character references expand
print "[" + xml.text(xml.parse("<a>&lt;&gt;&amp;&apos;&quot;&#65;&#x42;</a>")) + "]"
' a DTD with an entity declared and never used parses
print xml.parse("<!DOCTYPE a [<!ENTITY y 'Y'>]><a>plain</a>").children[0]
' in an attribute, libxml2 expands a declared entity itself, so nothing is lost
print xml.parse("<!DOCTYPE a [<!ENTITY y 'Y'>]><a k='1&y;2'/>").attrs.k
' an entity no DTD declares was, and is, a well-formedness error
d = xml.parse("<a>&q;</a>")
print "undeclared entity: " + error.message
error.clear()

doc = "<!DOCTYPE a [<!ENTITY y 'Y'>]>" + chr(10) + "<a><b>ok</b><c>&y;</c></a>"
f {file}= "examples/tmp_xml_refusals.xml"
write(f, doc)
d = xml.parse_file(f)
print "parse_file: " + error.message
error.clear()
r = xml.reader(f)
e = xml.read(r)
print "reader before it: " + e.name
e = xml.read(r)
e = xml.read(r)
print "reader text before it: " + e.text
e = xml.read(r)
e = xml.read(r)
e = xml.read(r)
print "reader at it: " + error.message
error.clear()
xml.close(r)
r = xml.reader(f)
ok = xml.skip_to(r, "b")
print "subtree before it: " + xml.text(xml.subtree(r))
ok = xml.skip_to(r, "c")
t = xml.subtree(r)
print "subtree holding it: " + error.message
error.clear()
xml.close(r)
delete(f)

' --- row 12: xml.text of a miss --------------------------------------------
t = xml.parse("<a><b><c>x</c></b><e></e></a>")
s = xml.text(xml.find(t, "c"))
print "text of a miss: " + error.message
error.clear()
s = xml.text(5)
print "text of a number: " + error.message
error.clear()
' the neighbours: a hit, a hit with nothing in it, the path written in full,
' and a text child taken from the children list
print "[" + xml.text(xml.find(t, "b/c")) + "]"
print "[" + xml.text(xml.find(t, "e")) + "]"
print "[" + xml.text(xml.find(t, "b")) + "]"
print "[" + xml.text(xml.find(t, "b/c").children[0]) + "]"
' the shape a miss is meant to be met in
n = xml.find(t, "c")
if is_unknown(n) then
    print "a miss is decided before xml.text"
end if
print "no error after it: " + string(not error)
