' PLAT-STREAM child fixture: normal stdout, then a terminal runtime error.
' Used to prove --line-buffered and --json-diagnostics are orthogonal: the flag
' touches stdout buffering only and must not perturb the diagnostic stream.
print "before"
x = 0
y = 1 / x
print "after"
