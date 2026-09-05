' An alias may not claim the name of a library already loaded under its own
' name. Same rule, other participants.
load scope_alpha from "libs/scope_alpha.bas"
load scope_beta from "libs/scope_beta.bas" as scope_alpha
print "unreachable"
