' A library loaded under its own name, which is a built-in module's: every
' `csv.` call would reach the module, so the library could never be called.
' It used to load without a word (2026-11-24).
load csv from "libs/native_named.bas"
print "unreachable"
