' `dim` is a legal FIELD name -- as every other keyword is. It is refused only
' where a STATEMENT was expected, which is the position its advice is about.
r = { dim: 7, end: 8 }
print(r.dim)
print(r["dim"])
r.dim = 9
print(r.dim)
p = { a: { dim: 1 } }
print(p.a.dim)
