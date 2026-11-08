' ari_discover against the gBASIC books' Chapter 8 findings (book gaps G31,
' G32), on synthetic corpora shaped like the book's registers. Every answer is
' written by hand. Run by tests/run_ari_discover.sh.
load ari_discover
load ari

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

function spec_line(spec, needle)
    for each ln in split(spec, "\n")
        if contains(ln, needle) then
            return trim(ln)
        end if
    end for
    return ""
end function

' ---- G31: a mostly-constant column, a literal in some files ----------------
function flag_src(id, flags)
    t = "CALL REGISTER" + chr(10) + chr(10)
    i = 0
    for each fl in flags
        i = i + 1
        t = t + "C41800" + string(i) + "   R-10000" + string(i) + "   " + fl + "   " + string(10 + i) + ".50   " + string(100 + i) + ".25" + chr(10)
    end for
    return { id: id, text: t }
end function
mostly_n = ["N","N","N","N","N","N","N","N","N","Y"]
mixed = ["N","Y","N","Y","Y","N","Y","N","Y","N"]
p31 = ari_discover.infer([ flag_src("oct", mostly_n), flag_src("nov", mixed), flag_src("dec", mostly_n),
                           flag_src("jan", mixed), flag_src("feb", mixed) ])
check("G31 the refusal stands", p31.ok, false)
check("G31 and names the near miss", contains(p31.why, "differ only at token 5 (`N` against `<TEXT>`)"), true)

' ---- G32: the heading is not a row ------------------------------------------
function call_src(id)
    t = "SERVICE CALL REGISTER" + chr(10) + chr(10)
    t = t + "  CALL      CUSTOMER          HOURS     PARTS" + chr(10)
    i = 0
    while i < 8
        i = i + 1
        t = t + "  C41800" + string(i) + "   CUSTOMER " + chr(64 + i) + "        " + string(i) + ".50     " + string(100 + i) + ".25" + chr(10)
    end while
    return { id: id, text: t }
end function
cs = [ call_src("a"), call_src("b"), call_src("c"), call_src("d"), call_src("e") ]
p32 = ari_discover.infer(cs)
check("G32 the start pattern does not match the `CALL` heading", spec_line(p32.spec, "starts("), "section rows repeats starts(/^[ ]*C[0-9]/):")
check("G32 and the rows read are the 8 calls, not 9", count(ari.parse(cs[0].text, p32.spec).value.rows), 8)

' ---- G32: column spans from the printed rule -------------------------------
function rule_src(id, longname)
    t = "SERVICE REGISTER" + chr(10) + chr(10)
    t = t + "  CALL      CUSTOMER                  PARTS" + chr(10)
    t = t + "  -------   ------------------------  ---------" + chr(10)
    i = 0
    for each n in ["SMITH, J", "OKAFOR, B", "LI, W", "DIAZ, R", "KOWALSKI, P", "NGUYEN, T"]
        i = i + 1
        t = t + "  C41800" + string(i) + "   " + n + repeat(" ", 26 - len(n)) + string(100 + i) + ".25" + chr(10)
    end for
    if longname then
        t = t + "  C418009   VANDERWESTHUIZEN, PIETER  109.25" + chr(10)
    end if
    return { id: id, text: t }
end function
rp = ari_discover.infer([ rule_src("a", false), rule_src("b", false), rule_src("c", false),
                          rule_src("d", false), rule_src("f", false) ], { allow_fixed_columns: true })
check("G32 the customer column is the rule's run, not its widest value", spec_line(rp.spec, "field customer"), "field customer: columns 12-35")
later = ari.parse(rule_src("e", true).text, rp.spec).value.rows
check("G32 so a longer name in a later file reads whole", later[count(later) - 1].customer, "VANDERWESTHUIZEN, PIETER")

' ---- G32: a run's totals, one per money column -----------------------------
function depot_src(id, seed)
    t = "SERVICE CALL REGISTER                                   PAGE    1" + chr(10)
    t = t + "ACME FIELD SERVICE                                      RUN 09/16/2026 06:12" + chr(10)
    t = t + "========================================================================" + chr(10) + chr(10)
    d = 0
    totals = []
    while d < 3
        d = d + 1
        t = t + "DEPOT  " + string(10 + d + seed) + "  NORTH" + chr(10) + chr(10)
        t = t + "  CALL        CUSTOMER                      HOURS          PARTS" + chr(10)
        t = t + "  ----------  ----------------------   ---------   ------------" + chr(10)
        i = 0
        hours = 0
        parts = 0
        while i < 6
            i = i + 1
            h = i + d + seed
            pt = 100 * i + 7 * d + seed
            hours = hours + h
            parts = parts + pt
            hs = string(h) + ".50"
            ps = string(pt) + ".25"
            t = t + "  0014" + string(1000 + i * 7 + d) + "    CUSTOMER " + chr(64 + i) + repeat(" ", 15) + repeat(" ", 12 - len(hs)) + hs + repeat(" ", 15 - len(ps)) + ps + chr(10)
        end while
        ht = string(hours + 3) + ".00"
        pt2 = string(parts + 1) + ".50"
        append(totals, { hours: ht, parts: pt2 })
        t = t + chr(10) + "  DEPOT TOTAL" + repeat(" ", 26) + repeat(" ", 12 - len(ht)) + ht + repeat(" ", 15 - len(pt2)) + pt2 + chr(10) + chr(10)
    end while
    return { id: id, text: t, totals: totals }
end function
ds = [ depot_src("a", 0), depot_src("b", 1), depot_src("c", 2), depot_src("d", 3), depot_src("e", 4), depot_src("f", 5) ]
dp = ari_discover.infer(ds)
g = ari.parse(ds[0].text, dp.spec).value.groups[0]
check("G32 the hours total is read as the hours total", string(g.group_total_hours), ds[0].totals[0].hours)
check("G32 and the parts total is read at all", string(g.group_total_parts), ds[0].totals[0].parts)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
