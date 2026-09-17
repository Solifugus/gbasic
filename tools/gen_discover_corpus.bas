' Generate a CORPUS of branch-activity reports for ARI Discover, together with
' the ANSWER KEY that says what is in them.
' (docs/ari_discover_design.md §15; see examples/fixtures/ari_discover/MANIFEST.md.)
'
'   gbasic tools/gen_discover_corpus.bas [outdir] [count] [seed]
'
'     outdir   directory to write into       (default examples/fixtures/ari_discover)
'     count    reports to emit               (default 24)
'     seed     RNG seed                      (default 20260916)
'
' WHY THIS EXISTS AND WHY IT IS NOT gen_teller_report.bas
'
' The two existing ARI generators emit a REPORT. That is everything a parser
' test needs, because the answer key for a parser test is the spec somebody
' wrote by hand. Discovery has no such spec -- inferring one IS the thing under
' test -- so a corpus with no truth beside it can only be scored against what
' discovery itself claims, which is a transcript and not a measurement.
'
' ONE DECLARATION PRODUCES BOTH THE REPORT AND THE TRUTH. That rule is R1 of
' stdlib/estate.bas and it is here for the same reason: a hand-written answer
' key drifts the first time either side changes, and A FIXTURE WHOSE ANSWER KEY
' IS WRONG TEACHES THE TOOL TO BE WRONG AND THEN CERTIFIES IT. Every value in
' truth.json below is the value this program planted, not a value read back out
' of the text it printed.
'
' WHAT THE TRUTH CARRIES, and why each part is needed to score §8's scorecard:
'
'   furniture_lines   1-based physical lines this generator INSERTED as page
'                     furniture. Scores furniture detection as precision AND
'                     recall exactly, rather than "it found some headers".
'   families          how many lines of each row family. Scores row-family
'                     recall, and catches the failure §7 Phase 4 warns about --
'                     a minority family absorbed into the dominant one.
'   branches[]        THE PLANTED VALUES: every account, name, date and amount.
'                     This is the strong oracle §15.4 asks for -- an accepted
'                     specification, run through ordinary `ari.parse`, must
'                     recover these. Coverage percentages cannot substitute:
'                     a spec can claim every line and extract the wrong number.
'   variant           the drift axes this source was generated under, so a
'                     failure can be attributed to an axis rather than noticed.
'
' THE DRIFT AXES ARE THE POINT (§15.2). A generator's natural output is
' regular, which is exactly wrong here: discovery scored on a uniform corpus
' measures nothing, because every accidental constant looks like a real one.
' Nine axes vary across sources -- money notation, heading wording, column
' shift, optional sections, page length, form feeds vs header-only, date
' dialect, description width, and the total's label.
'
' PAGINATION IS A SEPARATE PASS over the finished line list, driven purely by a
' line count and blind to content, so page breaks land mid-table and mid-branch.
' That is gen_teller_report.bas's rule and it is load-bearing for the same
' reason: breaking at tidy boundaries would quietly destroy the property the
' fixture exists to test.
'
' DETERMINISM. Output is a pure function of (outdir, count, seed): seeded RNG,
' no clock, a fixed run stamp. The same arguments produce byte-identical files,
' so this can back a golden.

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

' Cents as an exact integer, so no float rounding reaches the page or the key.
function money_text(cents, style)
  neg = cents < 0
  a = abs(cents)
  whole = floor(a / 100)
  frac = a - whole * 100
  body = group_digits(whole) + "." + pad_left(string(frac), 2)
  body = replace(body, " ", "0")
  if not neg then
    return body
  end if
  if style = "trailing_minus" then
    return body + "-"
  end if
  if style = "parens" then
    return "(" + body + ")"
  end if
  return "-" + body
end function

function month_abbr(m)
  names = [ "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" ]
  return names[m - 1]
end function

' Days are kept to 13..28 ON PURPOSE. Below 13 a MM/DD value is also a valid
' DD/MM value, so a wrong reading yields a plausible wrong date rather than an
' error -- which is the residue ARI's `using date:` exists for, and not what
' this corpus is measuring. Discovery should have to settle the DIALECT from
' the corpus, not be rescued by an impossible day.
function zero2(n)
  return replace(pad_left(string(n), 2), " ", "0")
end function

' Zero-padded, never space-padded. Both occur in real reports, but a
' space-padded month is an UNDECLARED axis -- it silently varies the field's
' apparent width without appearing in the variant record, so a discovery
' failure caused by it could not be attributed. Every axis in this corpus is
' declared; if space padding is wanted later it becomes a tenth axis.
function date_text(m, d, style)
  if style = "dmmmy" then
    return zero2(d) + "-" + month_abbr(m) + "-2026"
  end if
  return zero2(m) + "/" + zero2(d) + "/2026"
end function

' ---------------------------------------------------------------- content

function surnames()
  return [ "MERCER", "OKAFOR", "LINDQVIST", "ROSSI", "SANTOS", "NAKAMURA",
           "ABIODUN", "PETROVA", "CHAUDHRY", "WHITFIELD", "DELACROIX",
           "ANDERSSON", "MWANGI", "KOVACS", "REYES", "THORNBURY" ]
end function

function forenames()
  return [ "ALICE", "BRENDAN", "EVA", "CHIARA", "DIEGO", "YUKI", "FEMI",
           "IRINA", "NADIA", "MARGARET", "PHILIPPE", "GUNNAR", "ESTHER",
           "LASZLO", "CARMEN", "WINIFRED" ]
end function

function branch_names()
  return [ "RIVERSIDE", "LAKESIDE", "NORTHGATE", "OLD MILL", "CEDAR PARK",
           "HARBOUR", "STONEBRIDGE", "WESTFIELD" ]
end function

' One branch block. Returns { lines, truth, families } -- the lines to print,
' the values planted in them, and a tally of how many lines of each family.
'
' `lines` is built and returned rather than appended to a parameter: inside a
' function `append` mutates a LOCAL COPY of an array argument, so a
' mutate-the-caller's-array helper would silently do nothing.
function build_branch(v, bnum, bname)
  lines = []
  fam = { section_heading: 0, column_heading: 0, rule: 0, detail: 0,
          total: 0, remarks: 0, blank: 0 }

  ind = repeat(" ", v.col_shift)

  append(lines, "BRANCH " + pad_left(string(bnum), 3) + "  " + bname)
  fam.section_heading = fam.section_heading + 1
  append(lines, "")
  fam.blank = fam.blank + 1

  acct_label = v.acct_label
  append(lines, ind + "  " + pad_right(acct_label, 12)
                + pad_right("MEMBER NAME", v.name_width + 3)
                + pad_right("POSTED", 13) + pad_left("AMOUNT", 12))
  fam.column_heading = fam.column_heading + 1
  append(lines, ind + "  " + pad_right(repeat("-", 10), 12)
                + pad_right(repeat("-", v.name_width), v.name_width + 3)
                + pad_right(repeat("-", 10), 13) + pad_left(repeat("-", 12), 12))
  fam.rule = fam.rule + 1

  sn = surnames()
  fn = forenames()
  rows = 4 + random_int(0, 5)
  accounts = []
  total_cents = 0
  i = 0
  while i < rows
    acct = "001" + pad_left(string(40000 + random_int(0, 9999)), 5)
    who = sn[random_int(0, count(sn) - 1)] + ", " + fn[random_int(0, count(fn) - 1)]
    mo = 1 + random_int(0, 11)
    da = 13 + random_int(0, 15)
    cents = 2500 + random_int(0, 480000)
    if random_int(0, 5) = 0 then
      cents = 0 - cents
    end if
    total_cents = total_cents + cents

    posted = date_text(mo, da, v.date_style)
    amt = money_text(cents, v.money_style)
    append(lines, ind + "  " + pad_right(acct, 12)
                  + pad_right(who, v.name_width + 3)
                  + pad_right(posted, 13) + pad_left(amt, 12))
    fam.detail = fam.detail + 1
    append(accounts, { account: acct, name: who,
                       posted: posted, amount_cents: cents, amount_text: amt })
    i = i + 1
  end while

  append(lines, "")
  fam.blank = fam.blank + 1
  append(lines, ind + "  " + pad_right(v.total_label, 12 + v.name_width + 3 + 13)
                + pad_left(money_text(total_cents, v.money_style), 12))
  fam.total = fam.total + 1

  remarks_text = ""
  if v.remarks then
    notes = [ "posted late by operations",
              "reconciled against the general ledger",
              "two items held for review",
              "branch closed early for maintenance" ]
    remarks_text = notes[random_int(0, count(notes) - 1)]
    append(lines, "")
    fam.blank = fam.blank + 1
    append(lines, ind + "  REMARKS:")
    fam.remarks = fam.remarks + 1
    append(lines, ind + "    " + remarks_text)
    fam.remarks = fam.remarks + 1
  end if

  append(lines, "")
  fam.blank = fam.blank + 1

  return { lines: lines,
           families: fam,
           truth: { number: bnum, name: bname,
                    total_cents: total_cents,
                    total_text: money_text(total_cents, v.money_style),
                    remarks: remarks_text,
                    accounts: accounts } }
end function

' ---------------------------------------------------------------- pagination
'
' A SEPARATE PASS, BLIND TO CONTENT. It counts lines and nothing else, so a
' page break lands wherever it lands -- mid-table, mid-branch, between a label
' and its value. Breaking at tidy boundaries would produce a corpus that is
' bigger and strictly EASIER than a real report.
'
' Returns { lines, furniture } where furniture holds the 1-BASED indices of
' every line this pass inserted. That list is the answer key for §7 Phase 3.
function paginate(content, v, stamp)
  out = []
  furniture = []
  page = 1
  used = 0
  n = count(content)
  i = 0
  while i < n
    if used = 0 then
      if page > 1 and v.form_feed then
        append(out, chr(12))
        append(furniture, count(out))
      end if
      append(out, pad_right("BRANCH ACTIVITY REGISTER", 56)
                  + "PAGE " + pad_left(string(page), 4))
      append(furniture, count(out))
      append(out, pad_right("MIDLAND MUTUAL CREDIT UNION", 56)
                  + "RUN " + stamp)
      append(furniture, count(out))
      append(out, repeat("=", 72))
      append(furniture, count(out))
      append(out, "")
      append(furniture, count(out))
      used = 4
    end if
    append(out, content[i])
    used = used + 1
    i = i + 1
    if used >= v.page_len then
      used = 0
      page = page + 1
    end if
  end while
  return { lines: out, furniture: furniture, pages: page }
end function

' ---------------------------------------------------------------- variants
'
' Nine axes. The variant is chosen by INDEX rather than at random so the corpus
' covers each axis evenly and a reader can say which source exercises what --
' a randomly drawn corpus can leave an axis with one sample or none, and then a
' discovery failure on that axis looks like luck.
function variant_for(k)
  money_styles = [ "plain", "trailing_minus", "parens" ]
  acct_labels = [ "ACCT", "ACCOUNT", "ACCT NO" ]
  total_labels = [ "BRANCH TOTAL", "TOTAL FOR BRANCH", "BRANCH TOTALS" ]
  date_styles = [ "mdy", "dmmmy" ]
  shifts = [ 0, 2, 4 ]
  widths = [ 22, 28 ]
  page_lens = [ 50, 60, 66 ]

  return { money_style: money_styles[k - floor(k / 3) * 3],
           acct_label: acct_labels[floor(k / 3) - floor(floor(k / 3) / 3) * 3],
           total_label: total_labels[floor(k / 5) - floor(floor(k / 5) / 3) * 3],
           date_style: date_styles[floor(k / 2) - floor(floor(k / 2) / 2) * 2],
           col_shift: shifts[floor(k / 4) - floor(floor(k / 4) / 3) * 3],
           name_width: widths[floor(k / 7) - floor(floor(k / 7) / 2) * 2],
           page_len: page_lens[floor(k / 6) - floor(floor(k / 6) / 3) * 3],
           form_feed: (k - floor(k / 2) * 2) = 0,
           remarks: (floor(k / 3) - floor(floor(k / 3) / 2) * 2) = 0 }
end function

' ---------------------------------------------------------------- driver

program main( args )
  outdir = "examples/fixtures/ari_discover"
  count_n = 24
  sd = 20260916

  if count(args) > 0 then
    outdir = args[0]
  end if
  if count(args) > 1 then
    count_n = number(args[1])
  end if
  if count(args) > 2 then
    sd = number(args[2])
  end if

  seed(sd)
  bn = branch_names()
  manifest = []

  k = 0
  while k < count_n
    v = variant_for(k)
    ' ENOUGH BRANCHES THAT MOST SOURCES PAGINATE. Measured with 2-4: only 6 of
    ' 24 sources ran to a second page, so the furniture tier -- the one thing
    ' Phase 0 can be scored on exactly -- had six samples and eighteen sources
    ' on which the right answer is a refusal. Both cases are wanted; a corpus
    ' three-quarters weighted to the refusal is not.
    nbranches = 3 + random_int(0, 4)

    content = []
    branches = []
    fam = { section_heading: 0, column_heading: 0, rule: 0, detail: 0,
            total: 0, remarks: 0, blank: 0 }
    b = 0
    while b < nbranches
      bnum = 10 + random_int(0, 89)
      bname = bn[random_int(0, count(bn) - 1)]
      blk = build_branch(v, bnum, bname)
      for each ln in blk.lines
        append(content, ln)
      end for
      append(branches, blk.truth)
      for each key in keys(fam)
        fam[key] = fam[key] + blk.families[key]
      end for
      b = b + 1
    end while

    stamp = "09/16/2026 06:12"
    paged = paginate(content, v, stamp)

    id = pad_left(string(k + 1), 2)
    id = replace(id, " ", "0")
    name = id + "_branch_activity"
    path = outdir + "/" + name + ".rpt"
    f {file}= path
    write(f, join(paged.lines, "\n") + "\n")

    append(manifest, { id: name,
                       file: name + ".rpt",
                       variant: v,
                       pages: paged.pages,
                       physical_lines: count(paged.lines),
                       furniture_lines: paged.furniture,
                       families: fam,
                       branches: branches })
    k = k + 1
  end while

  t {file}= outdir + "/truth.json"
  write(t, encode({ generator: "tools/gen_discover_corpus.bas",
                    seed: sd,
                    count: count_n,
                    note: "PLANTED values, not values read back from the text. See the header of the generator.",
                    sources: manifest }) + "\n")

  print ("wrote " + string(count_n) + " reports and truth.json to " + outdir)
end program
