' Generate a synthetic teller-totals report for ARI fixtures.
' (docs/text_design.md §5.1 and §13.H; see examples/fixtures/ari/MANIFEST.md.)
'
'   gbasic tools/gen_teller_report.bas [branches] [tellers] [page] [ff] [seed]
'
'     branches  branch sections to emit          (default 3)
'     tellers   tellers per branch               (default 3)
'     page      lines per printed page           (default 66)
'     ff        1 = form feeds, 0 = header only  (default 1)
'     seed      RNG seed                         (default 42)
'
' WHY THIS EXISTS, AND WHAT IT MUST NOT BECOME
'
' The hand-made examples/fixtures/ari/teller_totals.rpt is the IRREGULARITY
' fixture: its value is that it is inconsistent with itself in the ways real
' reports are. A template generator that emitted tidy output would produce a
' file that is bigger and strictly EASIER, and ARI would pass on it and then
' fail on a real report. So this generator's job is scale and layout VARIATION,
' and it deliberately generates the awkward cases rather than smoothing them:
'
'   * Pagination is a SEPARATE PASS over the finished line list, driven purely
'     by a line count. It knows nothing about sections, so page breaks land
'     mid-table, mid-teller, and mid-branch -- which is what the real report
'     does and what §13.H is about. Generating page breaks at tidy boundaries
'     would quietly destroy the only property this fixture exists to test.
'   * Money format varies BY SECTION (§5.1), because a real report disagrees
'     with itself: different sections were written by different people decades
'     apart. Ten forms are emitted, including the ambiguous ones.
'   * Summary field ORDER differs between tellers, so a parser keyed to row
'     offsets reads the wrong number and reports no error.
'   * Column headings shift position between tables; the same field is spelled
'     `Teller #:` and `Teller#:`; identifiers are glued into prose columns
'     (CHK#4211); and some amounts are malformed on purpose, which per §8 must
'     degrade to `unknown` for that cell alone.
'
' DETERMINISM. Output is a pure function of the arguments -- seeded RNG, no
' clock, a fixed run date. The same arguments always produce byte-identical
' output, so this can back a golden.

' ---------------------------------------------------------------- formatting

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

' Insert thousands separators into a non-negative integer's digits.
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

' Absolute value as "1,234.56" -- digits only, no sign and no symbol.
function money_digits(amount)
  a = abs(amount)
  whole = floor(a)
  cents = floor((a - whole) * 100 + 0.5)
  if cents >= 100 then
    whole = whole + 1
    cents = cents - 100
  end if
  cs = string(cents)
  if cents < 10 then
    cs = "0" + cs
  end if
  return group_digits(whole) + "." + cs
end function

' The §5.1 money matrix. Each style is a different decade's idea of how to
' print an amount; a single report contains several of them.
'
'   0  $1,234.56            5  -$1,234.56
'   1  $    1,234.56        6  $-1,234.56
'   2  1,234.56             7  <$1,234.56>
'   3  1234.56              8  (1,234.56)
'   4  $1,234.56-           9  1,234.56CR
'
' Style 1 is the one that catches naive parsers: the symbol sits at the LEFT
' of a fixed field and the digits are right-justified inside it, so the gap is
' padding, not a delimiter.
function fmt_money(amount, style, width)
  d = money_digits(amount)
  neg = amount < 0

  if style = 1 then
    body = d
    if neg then
      body = "-" + d
    end if
    return "$" + pad_left(body, width - 1)
  end if

  if style = 3 then
    ' no grouping separators at all
    a = abs(amount)
    whole = floor(a)
    cents = floor((a - whole) * 100 + 0.5)
    cs = string(cents)
    if cents < 10 then
      cs = "0" + cs
    end if
    d = string(whole) + "." + cs
  end if

  s = ""
  if style = 0 then
    s = "$" + d
    if neg then
      s = "-$" + d
    end if
  end if
  if style = 2 then
    s = d
    if neg then
      s = d + "-"
    end if
  end if
  if style = 3 then
    s = d
    if neg then
      s = "-" + d
    end if
  end if
  if style = 4 then
    s = "$" + d
    if neg then
      s = "$" + d + "-"
    end if
  end if
  if style = 5 then
    s = "$" + d
    if neg then
      s = "-$" + d
    end if
  end if
  if style = 6 then
    s = "$" + d
    if neg then
      s = "$-" + d
    end if
  end if
  if style = 7 then
    s = "$" + d
    if neg then
      s = "<$" + d + ">"
    end if
  end if
  if style = 8 then
    s = d
    if neg then
      s = "(" + d + ")"
    end if
  end if
  if style = 9 then
    s = d
    if neg then
      s = d + "CR"
    end if
  end if
  return pad_left(s, width)
end function

' ------------------------------------------------------------------- content

function teller_name(n)
  firsts = ["Wendy", "Ryan", "Ada", "Marcus", "Priya", "Tomas", "Ines", "Cole"]
  lasts = ["Hermin", "Bellbowl", "Okonkwo", "Vasquez", "Lindqvist", "Ahmed"]
  a = firsts[n - floor(n / count(firsts)) * count(firsts)]
  b = lasts[n - floor(n / count(lasts)) * count(lasts)]
  return a + " " + b
end function

' One teller's block: summary, then a detail table.
function teller_block(lines, tnum, name, style, variant)
  rule = repeat("-", 79)

  append(lines, pad_right("Teller: " + name, 29) + "Teller #: " + string(tnum) + pad_left("Summary", 40 - len(string(tnum))))
  append(lines, rule)

  ' The summary fields, in one of three orders. Real reports differ here
  ' between tellers because the blocks were maintained separately.
  beginning = 3863.50 + tnum * 7.25
  ending = 8651.75 + tnum * 11.5
  total = 56870.50 + tnum * 103.75
  if variant = 0 then
    ending = 0 - ending
  end if

  b_line = pad_right("Beginning Cash", 60) + fmt_money(beginning, style, 19)
  e_line = pad_right("Ending Cash", 60) + fmt_money(ending, style, 19)
  t_line = pad_right("Total Transactions", 60) + fmt_money(total, style, 19)

  if variant = 0 then
    append(lines, b_line)
    append(lines, e_line)
    append(lines, t_line)
  end if
  if variant = 1 then
    append(lines, b_line)
    append(lines, t_line)
    append(lines, e_line)
  end if
  if variant = 2 then
    append(lines, t_line)
    append(lines, b_line)
    append(lines, e_line)
  end if

  append(lines, rule)

  ' The Amount heading shifts between tables -- a column-position parser breaks
  ' here, an anchor-relative one does not.
  indent = 26
  if variant = 1 then
    indent = 29
  end if
  append(lines, "GL                          Tran #    Type" + pad_left("Amount", indent))

  descs = ["Cash Deposit", "Check Deposit CHK#", "ACH Transfer", "Withdrawal", "Money Order MO#"]
  rows = 2 + random_int(0, 2)
  r = 0
  while r < rows
    d = descs[random_int(0, count(descs) - 1)]
    ' An identifier glued straight onto the label, no separator -- real, and
    ' awkward for anything that assumes a description is prose.
    if ends_with(d, "#") then
      d = d + string(3000 + random_int(0, 6999))
    end if
    amt = 250.0 + random_int(0, 30000) + random_int(0, 99) / 100
    if random_int(0, 4) = 0 then
      amt = 0 - amt
    end if
    append(lines, pad_right(d, 25) + pad_right("00100" + string(1000 + random_int(0, 8999)), 13) + pad_right(string(11 + random_int(0, 3)), 6) + fmt_money(amt, style, 35))
    r = r + 1
  end while
  append(lines, "")
  return lines
end function

' The closing-drawer grid: two side-by-side label/value columns, a different
' shape from the transaction table and a different anchoring problem.
function closing_block(lines, tnum, style, malformed)
  append(lines, "Closing Cash in Drawer     Teller#: " + string(tnum))
  append(lines, repeat("-", 79))
  append(lines, "Bills                     Coins")
  append(lines, "")
  bills = ["Hundreds", "Fifties", "Twenties", "Tens", "Fives", "Ones"]
  coins = ["Dollars", "Half-Dollars", "Quarters", "Dimes", "Nickles", "Pennies"]
  i = 0
  while i < count(bills)
    left_part = pad_right(bills[i], 10) + pad_left(string(random_int(0, 60)), 4)
    right_part = pad_right(coins[i], 14) + pad_left(string(random_int(0, 9)), 2)
    append(lines, pad_right(left_part, 26) + right_part)
    i = i + 1
  end while
  ' Bait cash is marked currency held for a robbery. Occasionally malformed on
  ' purpose: per §8 that cell must become `unknown`, never a silent zero.
  if malformed then
    append(lines, "Bait Cash   $8,0000")
  end if
  if not malformed then
    append(lines, "Bait Cash   " + fmt_money(8000, style, 0))
  end if
  append(lines, "")
  return lines
end function

' ------------------------------------------------------------------ assembly

' Build every content line, with NO page furniture. Pagination is a later,
' separate pass -- which is the whole point (see the header comment).
function build_content(branches, tellers)
  lines = []
  grand = 0
  b = 0
  while b < branches
    branch_no = 14 + b * 7
    ' Money style varies BY BRANCH, so one document contains several forms.
    style = b - floor(b / 10) * 10

    append(lines, "Branch: " + string(branch_no))
    append(lines, repeat("=", 79))

    branch_total = 0
    t = 0
    while t < tellers
      tnum = 261 + b * 40 + t * 13
      variant = t - floor(t / 3) * 3
      lines = teller_block(lines, tnum, teller_name(b * 5 + t), style, variant)
      branch_total = branch_total + 56870.50 + tnum * 103.75
      t = t + 1
    end while

    t = 0
    while t < tellers
      tnum = 261 + b * 40 + t * 13
      lines = closing_block(lines, tnum, style, random_int(0, 5) = 0)
      t = t + 1
    end while

    ' The rollup the hand-made fixture lacks: branch total, then a grand total
    ' at the end. Two levels of nesting for ARI to walk.
    append(lines, pad_right("Branch " + string(branch_no) + " Total", 60) + fmt_money(branch_total, style, 19))
    append(lines, "")
    grand = grand + branch_total
    b = b + 1
  end while

  append(lines, repeat("=", 79))
  append(lines, pad_right("GRAND TOTAL - ALL BRANCHES", 60) + fmt_money(grand, 0, 19))
  return lines
end function

' Insert page furniture every `page` lines. This pass is deliberately BLIND to
' the content: it counts lines and nothing else, exactly as a print spooler
' does, so breaks fall wherever they fall -- including through the middle of a
' detail table. ARI must strip these before anchoring (§13.H).
function paginate(lines, page, use_ff, run_stamp)
  out = []
  page_no = 1
  on_page = 0
  header = run_stamp + pad_left("Page " + string(page_no), 79 - len(run_stamp))
  if use_ff then
    append(out, chr(12) + header)
  end if
  if not use_ff then
    append(out, header)
  end if
  append(out, "")
  on_page = 2

  for each line in lines
    if on_page >= page then
      page_no = page_no + 1
      header = run_stamp + pad_left("Page " + string(page_no), 79 - len(run_stamp))
      if use_ff then
        append(out, chr(12) + header)
      end if
      if not use_ff then
        append(out, header)
      end if
      append(out, "")
      on_page = 2
    end if
    append(out, line)
    on_page = on_page + 1
  end for
  return out
end function

program main(args)
  branches = 3
  tellers = 3
  page = 66
  use_ff = true
  sd = 42

  if count(args) > 0 then
    branches = number(args[0])
  end if
  if count(args) > 1 then
    tellers = number(args[1])
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
  ' A fixed run stamp, never the clock: output must be reproducible. The "AM"
  ' on a 24-hour time is copied from the real report, which does that.
  content = build_content(branches, tellers)
  paged = paginate(content, page, use_ff, "07/15/2026 15:36 AM")
  for each line in paged
    print line
  end for
end program
