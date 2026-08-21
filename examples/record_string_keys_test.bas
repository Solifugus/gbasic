' Quoted record-literal keys (2026-08-20): a STRING key admits names an
' identifier cannot spell -- "content-type", "x y" -- closing a real
' asymmetry: decode() has always produced records with such keys from JSON,
' but the literal syntax could not write them. Read them back with the
' existing dynamic form, r["content-type"].

' hyphens, spaces, and mixing with plain and keyword keys
h = { "content-type": "text/html", "x y": 5, plain: 1, on: 2 }
print h["content-type"]
print h["x y"]
print h.plain
print h["on"]

' a quoted key that IS a valid identifier is the SAME field as the plain one
r = { "plain": 10 }
print r.plain
r2 = { plain: 1, "plain": 2 }
print r2.plain

' quoted keys work with = as well as :
e = { "a-b" = 7 }
print e["a-b"]

' escapes inside the key work as in any string
q = { "say \"hi\"": true }
print q["say \"hi\""]

' unicode key
u = { "prix en €": 9 }
print u["prix en €"]

' the round trip that motivated this: encode writes it, decode reads it back
enc = encode(h)
back = decode(enc)
print back["content-type"]
print back["x y"]

' has/keys/remove_key see quoted-key fields like any other
print has(h, "content-type")
print count(keys(h))
h2 = remove_key(h, "x y")
print has(h2, "x y")
