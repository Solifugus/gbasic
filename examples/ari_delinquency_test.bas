' ARI Phase 2 — the constructs teller_totals cannot reach.
'
' Runs against examples/fixtures/ari/delinquency.rpt, which is shaped to need
' exactly what the teller fixtures leave untested (see that file's entry in
' examples/fixtures/ari/MANIFEST.md):
'
'   down 1 of   OFFICER's label is alone on its line; the name is on the next.
'   up 2 of     BRANCH TOTAL prints its amount ABOVE the label, under a rule.
'   ranges      The gap after REMARKS: is 1, 2, 2 and 3 lines across the four
'               branches, so no exact distance matches them all.
'   flush       A NOTES line with a dotted leader to the right margin.
'   using/type  Dates are DD/MM/YYYY -- the one case the union recognizer
'               cannot settle.
'
' The date section is run TWICE on purpose: once undeclared, to show the
' ambiguous minority landing as `unknown` with diagnostics, and once with
' `using date: dmy`, to show the declaration settling them.

function show(v)
  if is_unknown(v) then
    return "unknown"
  end if
  return string(v)
end function

function cents(v)
  if is_unknown(v) then
    return "unknown"
  end if
  n = v * 100
  r = floor(n + 0.5)
  if n < 0 then
    r = 0 - floor(0 - n + 0.5)
  end if
  return string(r) + "c"
end function

' Everything except the date declaration, which differs between the two runs.
function base_spec(date_decl)
  sp = []
  append(sp, "page:")
  append(sp, "    break: formfeed")
  append(sp, "    drop: 3")
  append(sp, "")
  append(sp, "type euro_date:")
  append(sp, "    /[0-9]{2}\\/[0-9]{2}\\/[0-9]{4}/ -> dmy")
  append(sp, "    output: date")
  append(sp, "")
  append(sp, "section report:")
  append(sp, "    section regions repeats starts(/^REGION: /):")
  append(sp, "        field region: between \"REGION:\" and \"REGION CODE:\"")
  append(sp, "        field region_code: right of \"REGION CODE:\"")
  append(sp, "")
  append(sp, "        section branches repeats starts(/^  BRANCH /):")
  if date_decl != "" then
    append(sp, "            " + date_decl)
  end if
  append(sp, "            field branch_no: right of \"BRANCH\" as integer")
  ' The label is alone on its line; the value is the whole NEXT line.
  append(sp, "            field officer: down 1 of \"OFFICER\"")
  ' The amount sits two lines ABOVE its label, under a rule.
  append(sp, "            field branch_total: up 2 of \"BRANCH TOTAL\" as money")
  ' A dotted leader to the right margin.
  append(sp, "            field notes: right flush of /\\.\\.\\.\\. /")
  ' The gap varies by branch: 1, 2, 2, 3 lines. Only a range matches all four.
  append(sp, "            field remarks: down 1-3 of \"REMARKS:\"")
  append(sp, "")
  append(sp, "            section loans starts(/^    ACCOUNT/) ends(/^    -+$/):")
  append(sp, "                rows:")
  append(sp, "                    field account: columns 4-18")
  append(sp, "                    field member: columns 19-45")
  append(sp, "                    field opened: columns 46-56 as date")
  append(sp, "                    field balance: last money")
  return join(sp, "\n")
end function

program main(args)
  load ari from "../stdlib/ari.bas"

  f(file) = "examples/fixtures/ari/delinquency.rpt"
  report = join(read_lines(f), "\n")

  print "== undeclared dates: the ambiguous minority is refused, not guessed =="
  r = ari.parse(report, base_spec(""))
  print "ok        = " + r.ok
  v = r.value
  print "regions   = " + count(v.regions)

  for each rg in v.regions
    print "-- region " + trim(rg.region) + " (" + trim(rg.region_code) + ")"
    for each b in rg.branches
      print "   branch " + b.branch_no
      ' down 1 of
      print "     officer   = " + trim(show(b.officer))
      ' up 2 of
      print "     total     = " + cents(b.branch_total)
      ' right flush of
      print "     notes     = " + trim(show(b.notes))
      ' down 1-3 of  (the gap differs per branch)
      print "     remarks   = " + trim(show(b.remarks))
      ln = b.loans
      if not is_unknown(ln) then
        i = 0
        while i < count(ln.rows.account)
          print "     loan " + trim(ln.rows.account[i]) + " " + trim(ln.rows.member[i]) + " opened=" + show(ln.rows.opened[i]) + " bal=" + cents(ln.rows.balance[i])
          i = i + 1
        end while
      end if
    end for
  end for

  print ""
  print "diagnostics = " + count(r.diagnostics)
  for each d in r.diagnostics
    print "  " + d.reason + " at " + d.path + " (line " + d.line + ")"
  end for

  print ""
  print "== inspect: what the author should declare =="
  ins = ari.inspect(report, base_spec(""))
  for each fd in ins.findings
    line = "  " + fd.count + "x  " + fd.what
    if fd.hint != "" then
      line = line + "  -> " + fd.hint
    end if
    print line
  end for

  print ""
  print "== declared `using date: euro_date`: every date settles =="
  r2 = ari.parse(report, base_spec("using date: euro_date"))
  amb = 0
  for each d in r2.diagnostics
    if d.reason = "ambiguous-date" then
      amb = amb + 1
    end if
  end for
  print "ambiguous remaining = " + amb
  for each rg in r2.value.regions
    for each b in rg.branches
      ln = b.loans
      if not is_unknown(ln) then
        i = 0
        while i < count(ln.rows.opened)
          print "  " + trim(ln.rows.account[i]) + " opened=" + show(ln.rows.opened[i])
          i = i + 1
        end while
      end if
    end for
  end for
end program
