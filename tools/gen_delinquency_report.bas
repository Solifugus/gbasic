' Generate a synthetic loan delinquency register for ARI fixtures.
' (docs/text_design.md §5.1, §13.H; docs/ari_spec_language.md §4-§5.)
'
'   gbasic tools/gen_delinquency_report.bas [regions] [branches] [page] [ff] [seed]
'
'     regions   region sections                  (default 2)
'     branches  branches per region              (default 2)
'     page      lines per printed page           (default 60)
'     ff        1 = form feeds, 0 = header only  (default 1)
'     seed      RNG seed                         (default 7)
'
' WHY A SECOND REPORT TYPE
'
' teller_totals is a flat sequence of blocks whose every value sits on the same
' line as its label. That let ARI Phase 2 pass while four constructs in its own
' scope went unexercised. This report is deliberately shaped to need them, and
' it is a different SHAPE of document as well — deeply hierarchical (region ->
' branch -> loans) around a genuine table, which is the other common form these
' reports take.
'
' WHAT EACH PART EXISTS TO FORCE — nothing here is decoration:
'
'   * OFFICER on one line, the name on the NEXT. Requires `down 1 of "OFFICER"`.
'     Neither existing fixture has a value off its label's line, so the whole
'     vertical direction is currently untested.
'   * BRANCH TOTAL prints its amount ABOVE the label, under a rule line — the
'     ordinary layout for a totals block. Requires `up 2 of "BRANCH TOTAL"`.
'   * REMARKS is followed by a VARIABLE number of blank lines (0, 1 or 2 by
'     branch), so the note is 1 to 3 lines below its label. An exact distance
'     matches one branch and misses the others; this is what distance RANGES
'     are for, and it is the honest reason they exist rather than a synthetic
'     one.
'   * Dates are DD/MM/YYYY. This is the case the union recognizer genuinely
'     CANNOT settle: 03/04/2026 is 3 April or 4 March and nothing in the token
'     says which. It is the residue §5.1 predicted, and it is what `using date:`
'     and custom `type` blocks exist for. Days 13-28 are used for most rows so
'     a wrong reading produces a plausible wrong date rather than an error --
'     which is exactly why guessing is not acceptable.
'   * A NOTES line padded with dots to the right margin, for `flush`.
'
' Money here is deliberately UNIFORM (plain 1,234.56, parenthesised negatives) —
' the dialect-mixing case is already covered by gen_teller_report.bas, and
' repeating it would add size without adding coverage.
'
' DETERMINISM. Output is a pure function of the arguments: seeded RNG, fixed run
' date, no clock. Byte-identical across runs, so it can back a golden.

function pad_left(s, w)
  n = w - len(s)
  if n <= 0 then
    return s
  end if
  return repeat(" ", n) + s
end function

function pad_right(s, w)
  n = w - len(s)
  if n <= 0 then
    return s
  end if
  return s + repeat(" ", n)
end function

function group_digits(n)
  s = string(n)
  ln = len(s)
  head = ln - floor(ln / 3) * 3
  parts = []
  if head > 0 then
    append(parts, mid(s, 0, head))
  end if
  i = head
  while i < ln
    append(parts, mid(s, i, 3))
    i = i + 3
  end while
  return join(parts, ",")
end function

' Plain grouped amount; negatives in accounting parentheses.
function fmt_amount(amount, width)
  a = abs(amount)
  whole = floor(a)
  cents = floor((a - whole) * 100 + 0.5)
  cs = string(cents)
  if cents < 10 then
    cs = "0" + cs
  end if
  body = group_digits(whole) + "." + cs
  if amount < 0 then
    body = "(" + body + ")"
  end if
  return pad_left(body, width)
end function

function two(n)
  if n < 10 then
    return "0" + string(n)
  end if
  return string(n)
end function

' DD/MM/YYYY on purpose — see the header comment.
function fmt_date(d, m, y)
  return two(d) + "/" + two(m) + "/" + string(y)
end function

function member_name(n)
  lasts = ["SMITH", "GARCIA", "OKONKWO", "LINDQVIST", "AHMED", "VASQUEZ", "NAKAMURA", "OBRIEN"]
  firsts = ["JOHN Q", "MARIA", "TOMAS", "INES", "COLE", "PRIYA", "MARCUS", "ADA"]
  a = lasts[n - floor(n / count(lasts)) * count(lasts)]
  b = firsts[n - floor(n / count(firsts)) * count(firsts)]
  return a + ", " + b
end function

function officer_name(n)
  names = ["T. OKONKWO", "R. BELLBOWL", "A. LINDQVIST", "M. VASQUEZ"]
  return names[n - floor(n / count(names)) * count(names)]
end function

function branch_name(n)
  names = ["RIVERSIDE", "NORTHGATE", "LAKEVIEW", "STONEBRIDGE", "FAIRMONT"]
  return names[n - floor(n / count(names)) * count(names)]
end function

function region_name(n)
  names = ["SOUTHEAST", "NORTHWEST", "CENTRAL", "MOUNTAIN"]
  return names[n - floor(n / count(names)) * count(names)]
end function

function build_content(regions, branches)
  lines = []
  rule = repeat("-", 78)
  grand = 0

  r = 0
  while r < regions
    append(lines, "REGION: " + pad_right(region_name(r), 30) + "REGION CODE: " + "SE-" + two(4 + r * 3))
    append(lines, "")

    region_total = 0
    b = 0
    while b < branches
      bno = 142 + r * 30 + b * 7
      append(lines, "  BRANCH " + two(bno) + "  " + branch_name(r * 3 + b))

      ' The label sits alone on its line and the value is on the NEXT one.
      ' Requires `down 1 of "OFFICER"`.
      append(lines, "  OFFICER")
      append(lines, "  " + officer_name(r * 2 + b))
      append(lines, "  " + rule)

      ' TWO tables per branch, same column layout, each closed by a rule line.
      ' A section that `repeats` with an explicit `ends` needs more than one
      ' instance inside its parent before the combination proves anything, and
      ' one table per branch could not distinguish "found them all" from "found
      ' the first and stopped".
      tbl = 0
      branch_total = 0
      while tbl < 2
        if tbl = 0 then
          append(lines, "    CURRENT CYCLE")
        end if
        if tbl = 1 then
          append(lines, "")
          append(lines, "    PRIOR CYCLE")
        end if
        append(lines, "    ACCOUNT        MEMBER                     OPENED      BALANCE   DAYS")
        rows = 1 + random_int(0, 1)
        i = 0
        while i < rows
        acct = "00" + string(12000000 + random_int(0, 8999999))
        ' Days 1-28, deliberately spanning the 12 boundary. A day ABOVE 12 is
        ' self-disambiguating (27 cannot be a month), and a day at or below it
        ' is genuinely ambiguous — 03/04/2026 is 3 April or 4 March and nothing
        ' in the token decides. A real column contains both, which is exactly
        ' the situation the design has to handle: most values resolve on their
        ' own and a minority cannot, so an undeclared column comes out
        ' mostly-converted with a few unknowns rather than uniformly wrong.
        '
        ' An earlier revision generated days 13-28 only, which made EVERY date
        ' self-disambiguating and left the ambiguous case untested while
        ' claiming to cover it.
        dd = 1 + random_int(0, 27)
        mm = 1 + random_int(0, 11)
        yy = 2018 + random_int(0, 7)
        bal = 500.0 + random_int(0, 40000) + random_int(0, 99) / 100
        if random_int(0, 5) = 0 then
          bal = 0 - bal
        end if
        days = 31 + random_int(0, 120)
        branch_total = branch_total + bal
        append(lines, "    " + pad_right(acct, 15) + pad_right(member_name(r * 7 + b * 3 + i), 27) + fmt_date(dd, mm, yy) + fmt_amount(bal, 13) + pad_left(string(days), 7))
        ' Every other row WRAPS: a collateral note continues the record on the
        ' next physical line, indented past the account column. One record, two
        ' lines — which the grammar had no way to express before `continue(...)`.
        if i - floor(i / 2) * 2 = 0 then
          append(lines, "                   COLLATERAL: 2019 FORD F-150 VIN 1FTEW1E5XKKE" + string(10000 + random_int(0, 89999)))
        end if
        i = i + 1
        end while
        tbl = tbl + 1
      end while

      ' A totals block prints the amount ABOVE its label, under a rule.
      ' Requires `up 2 of "BRANCH TOTAL"`.
      append(lines, "    " + rule)
      append(lines, pad_left(fmt_amount(branch_total, 13), 65))
      append(lines, pad_left(repeat("=", 13), 65))
      append(lines, pad_left("BRANCH TOTAL", 64))
      append(lines, "")

      ' A dotted leader to the right margin, for `flush`.
      append(lines, "    NOTES " + repeat(".", 45) + " SEE SCHEDULE B")

      ' The gap after REMARKS: VARIES by branch (0, 1 or 2 blank lines), so the
      ' note is 1 to 3 lines below its label. An exact distance cannot match
      ' every branch; a range can.
      append(lines, "    REMARKS:")
      gap = (r + b) - floor((r + b) / 3) * 3
      g = 0
      while g < gap
        append(lines, "")
        g = g + 1
      end while
      notes = ["Member contacted; promised payment.",
               "Skip trace initiated.",
               "Payment plan agreed; first instalment due.",
               "Referred to collections."]
      append(lines, "      " + notes[random_int(0, count(notes) - 1)])
      append(lines, "")

      region_total = region_total + branch_total
      b = b + 1
    end while

    append(lines, pad_left(fmt_amount(region_total, 13), 65))
    append(lines, pad_left(repeat("=", 13), 65))
    append(lines, pad_left("REGION TOTAL", 64))
    append(lines, "")
    grand = grand + region_total
    r = r + 1
  end while

  append(lines, repeat("=", 78))
  append(lines, pad_left(fmt_amount(grand, 13), 65))
  append(lines, pad_left("GRAND TOTAL", 64))
  return lines
end function

' Pagination counts lines and is blind to content, so page breaks fall wherever
' they fall — including through the middle of a loan table or a totals block.
function paginate(lines, page, use_ff, run_date)
  out = []
  page_no = 1
  on_page = 0

  title = "CONSOLIDATED DELINQUENCY REGISTER"
  head1 = pad_right(title, 46) + "RUN " + run_date + pad_left("PAGE " + string(page_no), 12)
  if use_ff then
    append(out, chr(12) + head1)
  end if
  if not use_ff then
    append(out, head1)
  end if
  append(out, "FIRST TERRITORIAL CREDIT UNION")
  append(out, "")
  on_page = 3

  for each line in lines
    if on_page >= page then
      page_no = page_no + 1
      head1 = pad_right(title, 46) + "RUN " + run_date + pad_left("PAGE " + string(page_no), 12)
      if use_ff then
        append(out, chr(12) + head1)
      end if
      if not use_ff then
        append(out, head1)
      end if
      append(out, "FIRST TERRITORIAL CREDIT UNION")
      append(out, "")
      on_page = 3
    end if
    append(out, line)
    on_page = on_page + 1
  end for
  return out
end function

program main(args)
  regions = 2
  branches = 2
  page = 60
  use_ff = true
  sd = 7

  if count(args) > 0 then
    regions = number(args[0])
  end if
  if count(args) > 1 then
    branches = number(args[1])
  end if
  if count(args) > 2 then
    page = number(args[2])
  end if
  if count(args) > 3 then
    use_ff = number(args[3]) != 0
  end if
  if count(args) > 4 then
    sd = number(args[4])
  end if

  seed(sd)
  content = build_content(regions, branches)
  paged = paginate(content, page, use_ff, "03/04/2026")
  for each line in paged
    print line
  end for
end program
