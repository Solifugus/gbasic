' Two aliases may not claim one name. Effective library names are a flat
' namespace, so `t.` has to answer for exactly one library.
load toolkit from "libs/vendor_a/toolkit.bas" as t
load toolkit from "libs/vendor_b/toolkit.bas" as t
print "unreachable"
