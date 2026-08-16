' Recipe 6 — Check our formula engine against Excel's own answers.
'
' This is the single most useful call in the library and it has no equivalent
' in most spreadsheet tooling. Because every formula cell stores Excel's cached
' result, each formula can be checked IN ISOLATION -- no dependency graph, no
' recalculation -- by evaluating it and comparing. Run it on a workbook you were
' given, before trusting anything computed from it.

program main(args)
  wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")

  r = xlsx.check(wb, "Formulas")
  print "agree            = " + r.agree
  print "disagree         = " + r.disagree
  print "volatile skipped = " + r.volatile_skipped
  print "unsupported      = " + r.unsupported

  ' The three non-agreeing outcomes mean different things, and the distinction
  ' matters more than the totals:
  '
  '   disagree    -- we computed a DIFFERENT answer. A real defect, ours or a
  '                  misunderstanding of the function.
  '   volatile    -- NOW/TODAY/RAND. The cached value dates from whenever the
  '                  workbook last calculated, so it is not an oracle. Skipped
  '                  rather than judged.
  '   unsupported -- a function we do not implement. Reported BY NAME so the
  '                  gap is visible, never defaulted to a plausible zero.
  '
  ' Anything not agreeing is listed, never silently counted:
  print ""
  for each n in r.notes
    print "  " + n.verdict + " " + n.ref + "  " + n.formula
    ' `blocked_by` names the function that caused an `unsupported` verdict. It
    ' is present but EMPTY on the other verdicts, so test the string, not
    ' is_unknown -- this is what makes an unsupported function a to-do item
    ' with a name on it rather than an anonymous count.
    if not is_unknown(n.blocked_by) and n.blocked_by != "" then
      print "      blocked_by=" + n.blocked_by
    end if
  end for
end program
