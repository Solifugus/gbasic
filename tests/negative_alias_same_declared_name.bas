' TWO LIBRARIES, ONE DECLARED NAME, NO ALIAS. This used to fail as "function
' 'describe' is defined twice in library 'toolkit'", which named a function
' neither file had defined twice and said nothing about the two files being the
' problem. It is now one refusal that names both sources and spells the fix.
load toolkit from "libs/vendor_a/toolkit.bas"
load toolkit from "libs/vendor_b/toolkit.bas"
print "unreachable"
