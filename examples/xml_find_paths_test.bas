' A PATH IS TAKEN FROM EVERY ELEMENT EACH STEP REACHES (DOGFOOD row 8,
' 2026-11-08; fixed 2026-11-09). Both verbs followed only the first element at
' each step but the last, so find_all(d, "s/n") missed the n under every s but
' the first, and find missed an n that only a later s held. Every answer here
' is written out by hand from the document below.
load xml

d = xml.parse("<a><s><x>0</x></s><s><n>1</n><n>2</n></s><t><n>t</n></t><s><n>3</n><g><n>deep</n></g></s></a>")

function texts(list)
    out = ""
    for each e in list
        out = out + "[" + xml.text(e) + "]"
    end for
    return out
end function

print "s/n: " + texts(xml.find_all(d, "s/n"))
print "*/n: " + texts(xml.find_all(d, "*/n"))
print "s/*: " + texts(xml.find_all(d, "s/*"))
print "s/g/n: " + texts(xml.find_all(d, "s/g/n"))
print "s: " + string(count(xml.find_all(d, "s")))
' find is the first in document order, though the first s holds no n
print "find s/n: " + xml.text(xml.find(d, "s/n"))
print "find s/g/n: " + xml.text(xml.find(d, "s/g/n"))
print "find */n: " + xml.text(xml.find(d, "*/n"))
' a path is still a chain of CHILD names: n is no child of a
print "find n misses: " + string(is_unknown(xml.find(d, "n")))
print "find_all n: " + string(count(xml.find_all(d, "n")))
print "find s/q misses: " + string(is_unknown(xml.find(d, "s/q")))
print "find_all s/q: " + string(count(xml.find_all(d, "s/q")))
' slashes around and between steps are not steps
print "//s//n//: " + texts(xml.find_all(d, "//s//n//"))
' find and find_all agree: find is find_all's first, wherever it is
paths = ["s/n", "*/n", "s/*", "s/g/n", "t/n", "s/x", "*", "*/*", "*/*/n"]
agree = true
for each p in paths
    all = xml.find_all(d, p)
    if xml.text(xml.find(d, p)) != xml.text(all[0]) then
        agree = false
        print "differs: " + p
    end if
end for
print "find is find_all's first for each: " + string(agree)

' a path too long was cut short in silence, and could answer for a shorter one
on error goto next
long = repeat("s/", 64) + "n"
x = xml.find(d, long)
print "65 steps: " + error.message
error.clear()
x = xml.find_all(d, repeat("x", 1024))
print "1024 bytes: " + error.message
error.clear()
' the neighbours: 64 steps and 1023 bytes are taken (and miss)
print "64 steps: " + string(is_unknown(xml.find(d, repeat("s/", 63) + "n")))
print "1023 bytes: " + string(count(xml.find_all(d, repeat("x", 1023))))
