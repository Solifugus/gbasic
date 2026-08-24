' Keywords as DOT-access field names.
'
' `field_name` in the grammar already carried this list and already said why:
' a record-literal key and a position after `.` are CLOSED CONTEXTS -- nothing
' but a name can appear -- so admitting a keyword there costs no ambiguity.
' It was only ever wired into record LITERALS, which left the language able to
' BUILD a field it could not read: `{ end: 1 }` parsed, `r.end` did not.
'
' Four renames in shipped designs were forced by that gap.
program main( args )
    r = { end: 1, on: 2, to: 3, as: 4, in: 5, error: 6 }
    print string(r.end) + string(r.on) + string(r.to) + string(r.as) + string(r.in) + string(r.error)

    k = { function: 1, program: 2, library: 3, modifier: 4, watch: 5, watchers: 6 }
    print string(k.function) + string(k.program) + string(k.library) + string(k.modifier) + string(k.watch) + string(k.watchers)

    v = { step: 1, unknown: 2, unwatch: 3, spawn: 4, export: 5, consider: 6 }
    print string(v.step) + string(v.unknown) + string(v.unwatch) + string(v.spawn) + string(v.export) + string(v.consider)

    ' assignment through the dot, and nesting
    r.end = 9
    n = { on: { to: { error: "deep" } } }
    print "assigned " + string(r.end) + ", nested " + n.on.to.error

    ' the bracket form still reads the same field
    print "bracket agrees: " + string(r["end"])

    ' and none of it disturbs the keywords doing their real jobs
    if 1 = 1 then
        print "control flow intact"
    end if
end program
