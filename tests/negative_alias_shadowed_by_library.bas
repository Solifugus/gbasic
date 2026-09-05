' And the same collision in the other ORDER, because a rule that only holds one
' way around is a rule about statement order rather than about names.
load scope_beta from "libs/scope_beta.bas" as scope_alpha
load scope_alpha from "libs/scope_alpha.bas"
print "unreachable"
