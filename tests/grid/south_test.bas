' grid's gaps from the gBASIC books, volume 3, over
' examples/fixtures/xlsx/south.xlsx (tools/make_xlsx_south_fixture.py), every
' answer written by hand from the sheet's layout. Self-checking.
'   G19  a parent heading spans its merge range, and no further
'   G20  where every frame row came from, what was dropped and why, the header
'   G22  "<name> subtotal" is a total
load grid
load frame

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and got = want then
        return nothing
    end if
    G.mismatches = G.mismatches + 1
    print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    g = grid.of(xlsx.open("examples/fixtures/xlsx/south.xlsx"), "South")
    check("xlsx.grid reports the sheet's merges", count(g.merges), 2)
    r = grid.extract(g, { header_rows: 2, drop_totals: true, break_on_blank: false })
    names = []
    for each h in r.header
        append(names, h.name)
    end for
    check("G19: each heading under its own merge, Warranty under none", names,
          ["Visit Serial", "Visit Date", "Visit Tech", "Labour Hours", "Labour Rate", "Parts Cost", "Warranty",
           "Hours"])
    check("  and the header says which column each came from", r.header[7].col, 7)
    check("G20: the frame rows' sheet rows", r.rows, [3, 4, 7])
    check("  three visits, not five", count(frame.to_rows(r.frame)), 3)
    d = []
    for each x in r.dropped
        append(d, string(x.row) + " " + x.why)
    end for
    check("  and every row left out, with why", d, ["5 total", "6 blank", "8 total", "9 total"])
    check("  the dropped label is kept", r.dropped[0].label, "Garza subtotal")
    ' The blank row 6 splits the sheet into two blocks; the first holds rows 1-5,
    ' whose one total-like row is "Garza subtotal" (the old pattern counted 0).
    check("G22: tables() counts a subtotal",
          contains(string(grid.tables(g)), "contains 1 row(s) that look like totals"), true)
    pat = grid.total_pattern()
    tl = []
    for each t in ["Garza subtotal", "Okafor Subtotal", "Total", "Grand total", "sub-total:", "Total hours",
                   "Totalling machine", "Garza"]
        append(tl, contains(t, pat))
    end for
    check("  the pattern: a total word leading, or trailing the label", tl,
          [true, true, true, true, true, true, false, false])

    ' with merges ignored, the old carry: Warranty takes "Parts"
    old = grid.extract(g, { header_rows: 2, carry_parents: false })
    check("carry_parents: false, and the sheet has merges: merges still name the parents",
          old.header[1].name, "Visit Date")

    ' a heading written twice is two columns, and says so
    dup = grid.extract(g, { header_row: 2, drop_totals: true, break_on_blank: false })
    dn = []
    for each h in dup.header
        append(dn, h.name)
    end for
    check("a heading written twice is two columns", dn,
          ["Serial", "Date", "Tech", "Hours", "Rate", "Cost", "Warranty", "Hours (2)"])
    check("  and the notes say so", contains(string(dup.notes), "is written twice"), true)
    check("  each keeps its own values", frame.to_rows(dup.frame)[0]["Hours (2)"], 0.5)

    ' label_col: the drop check reads the label column the spec names
    lc = grid.extract(g, { header_row: 2, label_col: 2, drop_matching: regex("^Okafor$"), break_on_blank: false })
    check("drop_matching reads label_col", count(lc.dropped), 2)
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
