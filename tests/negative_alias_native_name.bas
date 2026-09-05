' The other direction: an alias may not BE a built-in module name, for the same
' reason -- the module dispatch would answer first and the aliased library
' would be unreachable through the name its loader chose.
load scope_alpha from "libs/scope_alpha.bas" as sqlite
print "unreachable"
