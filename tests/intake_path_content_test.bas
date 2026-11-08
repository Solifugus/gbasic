' identity "path+content" (book gap G38): sameness by name, with the bytes
' recorded beside it. Extracts named by their period were wrong under either
' rule alone: under "content" two empty days, byte for byte the same, were one
' file, so the second was skipped and the load stopped at a gap that did not
' exist; under "path" a period re-extracted with different bytes was a plain
' repeat. args[0] is an empty directory. Every answer is written by hand.
load intake

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    return nothing
end function

program main(args)
    checks = { n: 0, bad: 0 }
    base = args[0]
    drop = join_path(base, "drop")
    make_dir(drop, { parents: true })
    make_dir(join_path(base, "archive"), { parents: true })
    write(join_path(drop, "crm_2027-03-01.csv"), "id,name\n")
    write(join_path(drop, "crm_2027-03-02.csv"), "id,name\n")

    ' the control: under "content" the two empty days are one file
    c = intake.open(join_path(base, "content_ledger"), { identity: "content", ready: "renamed" })
    first = intake.arrived(c, drop, "*.csv")
    c = intake.taken(c, first[0], {})
    check("under content, taking one empty day takes the other with it", len(intake.arrived(c, drop, "*.csv")), 0)

    led = intake.open(join_path(base, "ledger"), { identity: "path+content", ready: "renamed" })
    a = intake.arrived(led, drop, "*.csv")
    check("under path+content they are two files", len(a), 2)
    for each e in a
        led = intake.taken(led, e, {})
        led = intake.archive(led, e, { to: {dir}join_path(base, "archive"), keep: 180 days })
    end for

    ' the 1st sent again, byte for byte; the 2nd re-extracted with a row
    write(join_path(drop, "crm_2027-03-01.csv"), "id,name\n")
    write(join_path(drop, "crm_2027-03-02.csv"), "id,name\n7,changed\n")
    led = intake.open(join_path(base, "ledger"), { identity: "path+content", ready: "renamed" })
    check("nothing arrives as new: both names were taken", len(intake.arrived(led, drop, "*.csv")), 0)
    r = intake.repeats(led, drop, "*.csv")
    check("both are repeats", len(r), 2)
    same = {}
    for each e in r
        same[file_name(e.path)] = e.same_content
    end for
    check("the resend has the same bytes", same["crm_2027-03-01.csv"], true)
    check("the re-extract does not: a conflict", same["crm_2027-03-02.csv"], false)

    print "checks: " + string(checks.n)
    print "mismatches: " + string(checks.bad)
end program
