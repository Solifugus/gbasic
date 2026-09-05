' `load NAME as ALIAS` -- the name a FILE calls a library by.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced. Every defect this
' feature can have returns a perfectly ordinary STRING from the wrong library:
' an alias that silently resolved to the first `toolkit` loaded would print
' "vendor A" twice, which is exactly what a golden would record as expected and
' then defend. So every check states the answer it wants and says which library
' it wanted it from.
load alias_host from "libs/alias_host.bas" as host
load toolkit from "libs/vendor_a/toolkit.bas" as ta
load toolkit from "libs/vendor_b/toolkit.bas" as tb
load scope_alpha from "libs/scope_alpha.bas"

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' THE LOAD-BEARING TIER. Two libraries whose own declared name is `toolkit`,
' from two paths, in one program. Before aliasing this was not a thing a
' program could express at all: the second import met the duplicate-function
' refusal and stopped.
'
' Asserted as a DIFFERENCE. "Both calls succeeded" is satisfied by an
' implementation that merged the two imports and answered from whichever won,
' so what is checked is that the two names reach DIFFERENT libraries.
check("an alias reaches the library it was loaded from", ta.describe(), "vendor A")
check("  and a second alias reaches the OTHER one", tb.describe(), "vendor B")
check("  so one declared name can arrive twice", ta.format(1) + "/" + tb.format(1), "A:1/B:1")
check("  each keeping its own functions", ta.only_a() + "/" + tb.only_b(), "a-only/b-only")

' AN ALIAS IS THE ONLY NAME. The library's own declared name is not additionally
' available, because two names for one import is the ambiguity this replaces
' rather than a convenience it adds. `toolkit.describe()` is a parse-clean call
' to a library nothing loaded under that name, and it raises -- pinned in
' negative_alias_declared_name_gone.bas, since a raise cannot be checked here.

' A LIBRARY DOES NOT KNOW IT WAS RENAMED. Its own unqualified calls, its
' qualified calls into its dependencies, and its exported modifiers all have to
' keep working, and all three are resolved by different code.
check("an aliased library still reaches its own functions", host.outer(), "host helper")
check("  and its dependency keeps ITS name", host.through_dep(), "dep")

greeting{shouted}= "hello"
check("an exported modifier survives the rename", greeting, "HELLO!")
qualified{host.shouted}= "hello"
check("  and is reachable through the alias", qualified, "HELLO!")

' THE CONTROL. An ordinary unaliased load is untouched -- without this the
' whole feature is satisfied by an implementation that renamed everything.
check("an unaliased library answers to its own name", scope_alpha.shared(), "alpha")
check("  and to its other functions", scope_alpha.only_alpha(), "alpha-only")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
