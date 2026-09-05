' A LIBRARY REACHES ITS OWN FUNCTIONS WITHOUT QUALIFYING. Everything else must
' be qualified, so this is the one exemption and it needs to hold in every
' shape a library can take.
library samefile
    function helper()
        return "same-file helper"
    end function
    function entry()
        ' THE CASE THE CROSS-LIBRARY SCAN WAS HIDING. A library declared in the
        ' same file as its program has no separate source path, so an own-first
        ' rule keyed on the FILE finds nothing here. Keyed on the LIBRARY it
        ' works, and until the scan was removed nobody could tell.
        return helper()
    end function
end library

load samefile
load scope_gamma from "libs/scope_gamma.bas"
load scope_delta from "libs/scope_delta.bas"

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

check("a library in its own file calls its own function", scope_gamma.outer(),
      "gamma inner")
check("  and gets ITS OWN, not the one loaded after it", scope_gamma.inner(),
      "gamma inner")
check("a library in the SAME FILE calls its own function", samefile.entry(),
      "same-file helper")

n{doubled}= 21
check("a modifier body calls its library's own function", n, 42)

' AND THE ROOT PROGRAM still calls its own functions unqualified -- the other
' thing that must not have been broken.
function mine()
    return "root"
end function
check("the root program calls its own function unqualified", mine(), "root")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
