' ARI Phase 2 — parse the hand-made teller totals fixture.
'
' The spec is docs/ari_spec_language.md §6, run for real. What it has to survive
' is in examples/fixtures/ari/MANIFEST.md, and none of it is incidental:
'
'   * The page header recurs mid-document and must be stripped before anchoring.
'   * Summary fields print in a DIFFERENT ORDER for different tellers, so an
'     offset-based read returns the wrong number for two of three and says
'     nothing. Anchoring by label is the whole point.
'   * The Amount heading is 4 columns adrift of its own data, and the negative
'     row is 1 column wider than the positives, so the amount is found by TYPE.
'   * `Teller #:` and `Teller#:` are the same field spelled two ways.
'   * Bait cash is malformed in one block and must become `unknown` alone.

' Money renders directly now. `as money` yields a NATIVE money value, and
' money prints its cents in full — 13586.25, not the "13586.2" a plain number
' gives above $9,999.99 (/DOGFOOD.md, 2026-08-01). An earlier revision of this
' test printed integer cents to work around exactly that; native conversion
' removed the need, so the golden now shows amounts as a reader would.
function amt(v)
  if is_unknown(v) then
    return "unknown"
  end if
  return string(v)
end function

program main(args)
  load ari from "../stdlib/ari.bas"

  sp = []
  append(sp, "page:")
  append(sp, "    break: /^[0-9]{2}\\/[0-9]{2}\\/[0-9]{4} .*Page [0-9]+$/")
  append(sp, "    drop: 2")
  append(sp, "")
  append(sp, "section report starts(/^Branch: /):")
  append(sp, "    field branch: right of \"Branch:\" as integer")
  append(sp, "")
  append(sp, "    section tellers repeats starts(/^Teller: /):")
  append(sp, "        field name: between \"Teller:\" and \"Teller #:\"")
  append(sp, "        field teller_no: right of \"Teller #:\" as integer")
  append(sp, "        field beginning_cash: right of \"Beginning Cash\" as money")
  append(sp, "        field ending_cash: right of \"Ending Cash\" as money")
  append(sp, "        field total_trans: right of \"Total Transactions\" as money")
  append(sp, "")
  append(sp, "        section detail starts(/^GL[ ]+Tran #/) ends(/^[ ]*$/):")
  append(sp, "            rows:")
  append(sp, "                field gl: columns 0-24")
  append(sp, "                field tran_no: columns 25-37")
  append(sp, "                field tran_ty: columns 38-43 as integer")
  append(sp, "                field amount: last money")
  append(sp, "")
  append(sp, "    section closing repeats starts(/^Closing Cash in Drawer/):")
  append(sp, "        field teller_no: right of \"Teller#:\" as integer")
  append(sp, "        field hundreds: right of \"Hundreds\" as integer")
  append(sp, "        field dollars: right of \"Dollars\" within columns 26-79 as integer")
  append(sp, "        field bait_cash: right of \"Bait Cash\" as money")
  spec = join(sp, "\n")

  f(file) = "examples/fixtures/ari/teller_totals.rpt"
  body = read_lines(f)
  report = join(body, "\n")

  print "== page furniture =="
  clean = ari.clean_grid(report, spec)
  print "raw lines   = " + count(body)
  print "clean lines = " + count(clean)
  kept = 0
  for each ln in clean
    hit = contains(ln, regex("Page [0-9]+$"))
    if hit then
      kept = kept + 1
    end if
  end for
  print "headers left= " + kept

  r = ari.parse(report, spec)
  print ""
  print "== parse =="
  print "ok          = " + r.ok
  if not r.ok then
    print "message     = " + r.message
    return
  end if

  v = r.value
  print "branch      = " + v.branch
  print "tellers     = " + count(v.tellers)
  ' NATIVE values, not lookalike strings (§4). `as money` yields a money value,
  ' which is what lets the output flow into frame/stats and what prints its
  ' cents in full.
  print "money kind  = " + type(v.tellers[0].beginning_cash)

  print ""
  print "== tellers (field ORDER differs between them) =="
  for each t in v.tellers
    print "-- teller " + t.teller_no + " (" + trim(t.name) + ")"
    print "   beginning = " + amt(t.beginning_cash)
    print "   ending    = " + amt(t.ending_cash)
    print "   total     = " + amt(t.total_trans)
    d = t.detail
    if is_unknown(d) then
      print "   detail    = none"
    else
      rows = d.rows
      print "   rows      = " + count(rows.amount)
      i = 0
      while i < count(rows.amount)
        print "     " + trim(rows.gl[i]) + " | " + trim(rows.tran_no[i]) + " | " + rows.tran_ty[i] + " | " + amt(rows.amount[i])
        i = i + 1
      end while
    end if
  end for

  print ""
  print "== closing drawers =="
  for each c in v.closing
    print "-- teller " + c.teller_no + " hundreds=" + c.hundreds + " dollars=" + c.dollars
    print "   bait_cash = " + amt(c.bait_cash)
  end for

  ' ------------------------------------------------------------------------
  ' The generated fixture: FORM FEEDS rather than a header pattern, three
  ' branches, and a different money dialect per branch. One recognizer covers
  ' the union (§5.1) — the spec never says which dialect a branch uses.
  print ""
  print "== generated fixture: form feeds + money union =="

  gp = []
  append(gp, "page:")
  append(gp, "    break: formfeed")
  append(gp, "    drop: 2")
  append(gp, "")
  append(gp, "section report:")
  append(gp, "    section branches repeats starts(/^Branch: /):")
  append(gp, "        field branch_no: right of \"Branch:\" as integer")
  append(gp, "        field branch_total: right of /^Branch [0-9]+ Total/ as money")
  append(gp, "")
  append(gp, "        section tellers repeats starts(/^Teller: /):")
  append(gp, "            field teller_no: right of \"Teller #:\" as integer")
  append(gp, "            field beginning_cash: right of \"Beginning Cash\" as money")
  append(gp, "            field ending_cash: right of \"Ending Cash\" as money")
  gspec = join(gp, "\n")

  g(file) = "examples/fixtures/ari/teller_totals_generated.rpt"
  gbody = read_lines(g)
  greport = join(gbody, "\n")

  gclean = ari.clean_grid(greport, gspec)
  print "raw lines   = " + count(gbody)
  print "clean lines = " + count(gclean)
  ff = 0
  for each ln in gclean
    hit = contains(ln, chr(12))
    if hit then
      ff = ff + 1
    end if
  end for
  print "form feeds left = " + ff

  gr = ari.parse(greport, gspec)
  print "ok          = " + gr.ok
  if gr.ok then
    gv = gr.value
    print "branches    = " + count(gv.branches)
    for each b in gv.branches
      print "-- branch " + b.branch_no + " total=" + amt(b.branch_total) + " tellers=" + count(b.tellers)
      ' Each branch prints money in a different dialect. The values must come
      ' out identical in kind regardless: plain, then symbol-with-inner-padding,
      ' then no-symbol-with-trailing-minus.
      for each t in b.tellers
        print "   teller " + t.teller_no + " begin=" + amt(t.beginning_cash) + " end=" + amt(t.ending_cash)
      end for
    end for
  end if
end program
