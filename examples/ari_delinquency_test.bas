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

function amt(v)
  if is_unknown(v) then
    return "unknown"
  end if
  return string(v)
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
  ' The SAME conversion by a different mechanism: a /re/repl/ transform
  ' rewrites DD/MM/YYYY to ISO with $-group references before the date
  ' conversion sees it, rather than naming a dialect. The two must agree.
  append(sp, "type rewritten_date:")
  append(sp, "    /([0-9]{2})\\/([0-9]{2})\\/([0-9]{4})/$3-$2-$1/ -> as date")
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
  ' REPEATS with an explicit ENDS: two tables per branch, each closed by a
  ' blank line or a rule. One instance could not distinguish "found them all"
  ' from "found the first and stopped".
  append(sp, "            section loans repeats starts(/^    ACCOUNT/) ends(/^[ ]*$|^    -+$/):")
  ' CONTINUE: every other row wraps onto a COLLATERAL line. One record, two
  ' physical lines. Rows without a wrap leave `collateral` unknown, which is
  ' correct and shows up as a diagnostic rather than a guess.
  append(sp, "                rows continue(/^[ ]+COLLATERAL/):")
  append(sp, "                    field account: columns 4-18")
  append(sp, "                    field member: columns 19-45")
  append(sp, "                    field opened: columns 46-56 as date")
  append(sp, "                    field balance: last money")
  append(sp, "                    field collateral: right of \"COLLATERAL:\"")
  return join(sp, "\n")
end function

program main(args)
  load ari from "../stdlib/ari.bas"

  f{file} = "examples/fixtures/ari/delinquency.rpt"
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
      print "     total     = " + amt(b.branch_total)
      ' right flush of
      print "     notes     = " + trim(show(b.notes))
      ' down 1-3 of  (the gap differs per branch)
      print "     remarks   = " + trim(show(b.remarks))
      print "     tables    = " + count(b.loans)
      ti = 0
      for each ln in b.loans
        print "     table " + ti + " rows=" + count(ln.rows.account)
        i = 0
        while i < count(ln.rows.account)
          coll = "-"
          if not is_unknown(ln.rows.collateral[i]) then
            coll = trim(ln.rows.collateral[i])
          end if
          print "       " + trim(ln.rows.account[i]) + " " + trim(ln.rows.member[i]) + " opened=" + show(ln.rows.opened[i]) + " bal=" + amt(ln.rows.balance[i]) + " coll=" + coll
          i = i + 1
        end while
        ti = ti + 1
      end for
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
  print "== ari.import: read the file itself, same answer as ari.parse =="
  ' The convenience entry point had never been called. It must agree with
  ' parse() exactly — the only difference is who reads the bytes.
  im = ari.import("examples/fixtures/ari/delinquency.rpt", base_spec(""))
  print "ok            = " + im.ok
  print "same lines    = " + (im.lines = r.lines)
  print "same regions  = " + (count(im.value.regions) = count(v.regions))
  print "same diags    = " + (count(im.diagnostics) = count(r.diagnostics))
  print "same 1st total= " + (im.value.regions[0].branches[0].branch_total = v.regions[0].branches[0].branch_total)

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
      for each ln in b.loans
        i = 0
        while i < count(ln.rows.opened)
          print "  " + trim(ln.rows.account[i]) + " opened=" + show(ln.rows.opened[i])
          i = i + 1
        end while
      end for
    end for
  end for

  print ""
  print "== regex-with-transform reaches the same answer as the dialect =="
  ' /re/repl/ and `-> dmy` are different mechanisms for one conversion. If they
  ' ever disagree, one of them is wrong and the golden says which.
  r3 = ari.parse(report, base_spec("using date: rewritten_date"))
  same = true
  ri = 0
  for each rg in r2.value.regions
    bi = 0
    for each b in rg.branches
      li = 0
      for each ln in b.loans
        i = 0
        while i < count(ln.rows.opened)
          other = r3.value.regions[ri].branches[bi].loans[li].rows.opened[i]
          if show(ln.rows.opened[i]) != show(other) then
            same = false
          end if
          i = i + 1
        end while
        li = li + 1
      end for
      bi = bi + 1
    end for
    ri = ri + 1
  end for
  print "transform agrees with dialect = " + same
  print "sample = " + show(r3.value.regions[0].branches[0].loans[0].rows.opened[0])
  ' Native kinds, not lookalike strings (§4).
  print "date kind  = " + type(r3.value.regions[0].branches[0].loans[0].rows.opened[0])
  print "money kind = " + type(r2.value.regions[0].branches[0].branch_total)
end program
