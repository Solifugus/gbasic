' `using <builtin>: <name>` -- the scoped override of §5, and the remedy `ari`
' itself prints for an ambiguous date.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced. Every defect this
' mechanism had produced A PLAUSIBLE FIELD: a date column that came back as
' ordinary text, a binding that did nothing, a directive that was dropped. Not
' one of them raised, and a golden would have recorded the damaged value as
' expected -- which is exactly how all three survived from the day the feature
' was written.
'
' WHAT WAS MEASURED BEFORE ANY OF IT WAS CHANGED:
'
'   using date: dmy   (in a section)  ->  the field came back as THE WHOLE RAW
'                                         LINE, as a string
'   using date: dmy   (at file scope) ->  silently ignored
'   using colour: x                   ->  silently ignored
'
' The first is the one that matters, because `inspect` tells authors to write
' it. The remedy for one silent wrong answer was itself a silent wrong answer.

load ari

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print ("ok   " + label)
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
    return nothing
end function

' An AMBIGUOUS date (03/04 is 3 April or 4 March) beside an UNAMBIGUOUS one
' (27 cannot be a month). Both are needed: the ambiguous one shows the binding
' acting, the unambiguous one shows it is a TIE-BREAK and not an override.
report = join([ "ROW 03/04/2026 1,234.56",
                "ROW 27/12/2026 2,000.00" ], "\n")

function spec_with(inner)
    body = [ "section report:",
             "    section rows repeats starts(/^ROW/):" ]
    for each l in inner
        append(body, "        " + l)
    end for
    append(body, "        field d: first date as date")
    return join(body, "\n")
end function

function dates_of(r)
    out = []
    if not r.ok then
        return out
    end if
    for each row in r.value.rows
        append(out, string(row.d))
    end for
    return out
end function

' ===========================================================================
print "-- the binding is a TIE-BREAK, asserted as a difference"
' ===========================================================================
' A binding that simply overrode the recognizer would also make the two runs
' differ. What separates the two is the SECOND row: 27/12 settles itself from
' the token alone and must read the same under either dialect.
undeclared = ari.parse(report, spec_with([]))
dmy = ari.parse(report, spec_with([ "using date: dmy" ]))
mdy = ari.parse(report, spec_with([ "using date: mdy" ]))

check("every spec parses", string(undeclared.ok) + string(dmy.ok) + string(mdy.ok),
      "truetruetrue")
check("undeclared, the ambiguous date is refused", dates_of(undeclared)[0], "unknown")
check("and it says why", undeclared.diagnostics[0].reason, "ambiguous-date")
check("dmy reads 03/04 as 3 April", dates_of(dmy)[0], "2026-04-03")
check("mdy reads it as 4 March", dates_of(mdy)[0], "2026-03-04")
check("the two dialects really disagree",
      dates_of(dmy)[0] = dates_of(mdy)[0], false)

' A DECLARATION IS AUTHORITATIVE, NOT A HINT, and the three readings of ONE
' unambiguous token are what say so. 27/12 settles itself from the token alone,
' so undeclared and `dmy` agree with each other -- and under `mdy` it is not
' quietly re-read day-first, it is REPORTED, because month 27 does not exist and
' the author has stated this column is month-first. Reinterpreting it would be
' guessing against an explicit declaration, which is the one thing this library
' refuses to do anywhere else.
'
' THIS CORRECTS WHAT THIS FIXTURE FIRST ASSERTED. The control was written as
' "an unambiguous date reads the same under either dialect", which sounds like
' the right shape for a tie-break and is false: it would require the mdy run to
' silently ignore the declaration.
check("an unambiguous date consistent with the declaration settles",
      dates_of(dmy)[1], "2026-12-27")
check("and reads the same undeclared, so the binding is a TIE-BREAK",
      dates_of(undeclared)[1], "2026-12-27")
check("one that CONTRADICTS the declaration is refused, not re-read",
      dates_of(mdy)[1], "unknown")
mdy_why = ""
for each d in mdy.diagnostics
    mdy_why = d.reason
end for
check("and the reason names the data, not the spec", mdy_why, "invalid-date")

' ===========================================================================
print ""
print "-- THE HINT AND THE MECHANISM, pinned to each other"
' ===========================================================================
' THE LOAD-BEARING TIER, because the defect was neither the hint nor the
' mechanism but the DRIFT BETWEEN THEM: `inspect` named a form the converter
' did not implement, and writing what it said returned the raw line as text.
' Asserting each half separately would have caught neither.
'
' So the spec below is BUILT FROM THE HINT `ari` prints, by lifting the
' backticked fragment out of it. If the hint changes wording, or the mechanism
' stops accepting what the hint names, this goes red.
ins = ari.inspect(report, spec_with([]))
hint = ""
for each f in ins.findings
    if contains(f.what, "ambiguous-date") then
        hint = f.hint
    end if
end for
check("an ambiguous date produces a hint", hint = "", false)

frag = match(hint, regex("`(using[^`]+)`"))
check("the hint names a `using` form", is_unknown(frag), false)
lifted = ari.parse(report, spec_with([ frag.groups[0] ]))
check("and writing exactly what the hint says parses", lifted.ok, true)
' GUARDED, because a perturbation that breaks the hint makes the parse FAIL and
' the two checks below would then index an empty list and take the whole fixture
' down -- a red tier that reports nothing is the failure this project has met
' before (run_discovery's own note). They must report their mismatch instead.
if lifted.ok then
    check("and settles the ambiguous date", dates_of(lifted)[0] = "unknown", false)
    check("as a real date, not as text",
          type(lifted.value.rows[0].d), "datetime")
else
    check("and settles the ambiguous date", "the spec was refused", "a settled date")
    check("as a real date, not as text", "the spec was refused", "datetime")
end if

' ===========================================================================
print ""
print "-- REFUSALS, each beside its nearest legal neighbour"
' ===========================================================================
' A refusal tier with no controls is satisfied by refusing every spec there is,
' and this mechanism's whole job is to ACCEPT three different kinds of target.

function refused(label, spec, needle)
    r = ari.parse(report, spec)
    tally.checks = tally.checks + 1
    if r.ok then
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": accepted, want refused")
        return nothing
    end if
    if contains(r.message, needle) then
        print ("ok   " + label)
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": message was " + r.message)
    end if
    return nothing
end function

refused("a target that names nothing is refused",
        spec_with([ "using date: nonsense" ]), "no type named `nonsense`")
refused("and the message names the dialects that exist",
        spec_with([ "using date: nonsense" ]), "dmy, mdy")
refused("rebinding something that is not a built-in type is refused",
        spec_with([ "using colour: dmy" ]), "not a built-in type")
refused("and the message names the built-in types",
        spec_with([ "using colour: dmy" ]), "date, money, integer, decimal, text")
refused("a `using` outside any section is refused",
        join([ "using date: dmy", spec_with([]) ], "\n"),
        "outside any section")
refused("a dialect of the wrong type is refused",
        spec_with([ "using money: dmy" ]), "no type named `dmy`")

' THE CONTROL THAT MATTERS MOST, and it is the Class A shape: a field whose
' binding names nothing must never come back as the RAW LINE. Before the
' refusal it did, silently, and the column read as perfectly ordinary text.
leak = ari.parse(report, spec_with([ "using date: nonsense" ]))
check("a field is never silently handed back the raw line", leak.ok, false)

' The three legal targets, all still accepted.
check("a dialect word is accepted", dmy.ok, true)
' NOTE the rule carries NO capture groups. A date rule that captures its
' components feeds only the FIRST of them to the recognizer and comes back
' `unknown` -- found writing this fixture, recorded as B9 in
' docs/ari_limitations.md rather than fixed here, because the register's sweep
' rule is Class A first and on its own.
declared = join([ "type euro_date:",
                  "    /[0-9]{2}\\/[0-9]{2}\\/[0-9]{4}/ -> dmy",
                  "    output: date",
                  spec_with([ "using date: euro_date" ]) ], "\n")
dr = ari.parse(report, declared)
check("a declared custom type is accepted", dr.ok, true)
check("and it converts through the type's own dialect",
      dates_of(dr)[0], "2026-04-03")
check("a spec with no `using` at all is untouched", undeclared.ok, true)

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))
