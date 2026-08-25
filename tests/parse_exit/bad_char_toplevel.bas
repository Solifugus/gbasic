' Same shape, reached through the OTHER door: a byte the lexer cannot tokenize
' at all. It already produced a located diagnostic; what it did not produce was
' a nonzero exit, which is why this is not a `dim`-specific bug.
print "before"
@
print "after"
