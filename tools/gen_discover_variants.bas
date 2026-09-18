' Generate the TELLER SESSION JOURNAL half of the ARI Discover VARIANT corpus,
' together with the answer key that says what is in it.
' (docs/ari_discover_design.md §13, §15; see examples/fixtures/ari_discover/MANIFEST.md.)
'
'   gbasic tools/gen_discover_variants.bas [outdir] [count] [seed]
'
'     outdir   directory to write into   (default examples/fixtures/ari_discover/variants)
'     count    reports to emit           (default 5)
'     seed     RNG seed                  (default 20260917)
'
' WHY ONLY HALF A CORPUS. §13's question is whether a corpus holds several
' legitimate report GRAMMARS, and answering it needs a corpus that does. The
' branch-activity half already exists and is used unchanged: the variant corpus
' is the first eight `NN_branch_activity.rpt` plus these teller journals, read
' from where they already live rather than regenerated here.
'
' That is not a shortcut. A second copy of the branch form would be a second
' thing that can drift, and a variant test whose two halves came from two
' generators could report a difference that neither form actually has. The
' branch sources in the variant corpus ARE the sources the rest of the suite
' scores against.
'
' WHAT MAKES THIS A VARIANT AND NOT DRIFT. The existing corpus already varies
' nine axes -- money notation, indent, date dialect, the total's label,
' pagination style -- and Phase 2 measured that ONE specification carries all
' of them, because none of them changes what the detail row IS. This form
' changes exactly that:
'
'     branch detail   <IDENTIFIER> <WORD> <WORD> <DATE> <MONEY>
'     teller detail   <NUMBER> <WORD> <IDENTIFIER> <MONEY> <MONEY>
'
' Different arity, different types, two money spans rather than one, and no
' date at all -- so a `posted` field cannot exist here however the rule is
' worded. An alternation cannot absorb that, and the point of the fixture is to
' find out whether the tool agrees.
'
' THIS HALF DRIFTS TOO, on five axes of its own. A variant group that was
' internally uniform would be strictly easier than the group it is compared
' against, and a tool that split the corpus for the wrong reason would pass.
'
' DETERMINISM. Output is a pure function of (outdir, count, seed).

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

function teller_names()
  return [ "MERCER, ALICE", "OKAFOR, FEMI", "LINDQVIST, GUNNAR",
           "ROSSI, CHIARA", "SANTOS, DIEGO", "NAKAMURA, YUKI",
           "PETROVA, IRINA", "WHITFIELD, MARGARET" ]
end function

function tran_codes()
  return [ "DEP", "WDL", "XFR", "CHK", "LOAN" ]
end function

' One teller session. Returns { lines, truth, families }.
'
' `lines` is built and returned rather than appended to a parameter: inside a
' function `append` mutates a LOCAL COPY of an array argument.
function build_session(v, tnum, drawer, who)
  lines = []
  fam = { section_heading: 0, column_heading: 0, rule: 0, detail: 0,
          total: 0, override: 0, blank: 0 }

  ind = repeat(" ", v.col_shift)

  append(lines, "TELLER " + pad_left(string(tnum), 3) + "  DRAWER "
                + string(drawer))
  fam.section_heading = fam.section_heading + 1
  append(lines, "")
  fam.blank = fam.blank + 1

  append(lines, ind + "  " + pad_right("SEQ", 6) + pad_right("TRAN", 7)
                + pad_right(v.acct_label, 15)
                + pad_left("AMOUNT", 13) + pad_left("BALANCE", 15))
  fam.column_heading = fam.column_heading + 1
  append(lines, ind + "  " + pad_right(repeat("-", 4), 6)
                + pad_right(repeat("-", 5), 7)
                + pad_right(repeat("-", 12), 15)
                + pad_left(repeat("-", 12), 13)
                + pad_left(repeat("-", 13), 15))
  fam.rule = fam.rule + 1

  tc = tran_codes()
  rows = 4 + random_int(0, 5)
  entries = []
  bal_cents = 250000 + random_int(0, 750000)
  net_cents = 0
  i = 0
  while i < rows
    acct = "001" + pad_left(string(40000 + random_int(0, 9999)), 5)
    code = tc[random_int(0, count(tc) - 1)]
    cents = 2500 + random_int(0, 480000)
    if random_int(0, 2) = 0 then
      cents = 0 - cents
    end if
    bal_cents = bal_cents + cents
    net_cents = net_cents + cents

    amt = money_text(cents, v.money_style)
    bal = money_text(bal_cents, v.money_style)
    append(lines, ind + "  " + pad_left(string(i + 1), 4) + "  "
                  + pad_right(code, 7) + pad_right(acct, 15)
                  + pad_left(amt, 13) + pad_left(bal, 15))
    fam.detail = fam.detail + 1
    append(entries, { seq: i + 1, tran: code, account: acct,
                      amount_cents: cents, amount_text: amt,
                      balance_cents: bal_cents, balance_text: bal })
    i = i + 1
  end while

  append(lines, "")
  fam.blank = fam.blank + 1
  append(lines, ind + "  " + pad_right(v.total_label, 28)
                + pad_left(money_text(net_cents, v.money_style), 13))
  fam.total = fam.total + 1

  override_text = ""
  if v.overrides then
    reasons = [ "limit raised by duty supervisor",
                "signature verified by second officer",
                "cash drawer recount agreed" ]
    override_text = reasons[random_int(0, count(reasons) - 1)]
    append(lines, "")
    fam.blank = fam.blank + 1
    append(lines, ind + "  OVERRIDE:")
    fam.override = fam.override + 1
    append(lines, ind + "    " + override_text)
    fam.override = fam.override + 1
  end if

  append(lines, "")
  fam.blank = fam.blank + 1

  return { lines: lines,
           families: fam,
           truth: { number: tnum, drawer: drawer, teller: who,
                    net_cents: net_cents,
                    net_text: money_text(net_cents, v.money_style),
                    override: override_text,
                    entries: entries } }
end function

' Pagination: a separate pass, blind to content, exactly as the branch
' generator does it, so page breaks land mid-table and mid-session.
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
      append(out, pad_right("TELLER SESSION JOURNAL", 56)
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

' Five axes, chosen by INDEX so the group covers each evenly.
function variant_for(k)
  money_styles = [ "plain", "trailing_minus", "parens" ]
  total_labels = [ "TELLER TOTAL", "TOTAL FOR TELLER" ]
  acct_labels = [ "ACCOUNT", "ACCT NO" ]
  shifts = [ 0, 2, 4 ]
  page_lens = [ 50, 60 ]

  return { money_style: money_styles[k - floor(k / 3) * 3],
           total_label: total_labels[floor(k / 2) - floor(floor(k / 2) / 2) * 2],
           acct_label: acct_labels[k - floor(k / 2) * 2],
           col_shift: shifts[floor(k / 3) - floor(floor(k / 3) / 3) * 3],
           page_len: page_lens[floor(k / 4) - floor(floor(k / 4) / 2) * 2],
           form_feed: (floor(k / 2) - floor(floor(k / 2) / 2) * 2) = 0,
           overrides: (k - floor(k / 3) * 3) = 0 }
end function

program main( args )
  outdir = "examples/fixtures/ari_discover/variants"
  count_n = 5
  sd = 20260917

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
  tn = teller_names()
  manifest = []

  k = 0
  while k < count_n
    v = variant_for(k)
    nsessions = 3 + random_int(0, 3)

    content = []
    sessions = []
    fam = { section_heading: 0, column_heading: 0, rule: 0, detail: 0,
            total: 0, override: 0, blank: 0 }
    b = 0
    while b < nsessions
      tnum = 10 + random_int(0, 89)
      drawer = 1 + random_int(0, 5)
      who = tn[random_int(0, count(tn) - 1)]
      blk = build_session(v, tnum, drawer, who)
      for each ln in blk.lines
        append(content, ln)
      end for
      append(sessions, blk.truth)
      for each key in keys(fam)
        fam[key] = fam[key] + blk.families[key]
      end for
      b = b + 1
    end while

    stamp = "09/17/2026 07:40"
    paged = paginate(content, v, stamp)

    id = "t" + replace(pad_left(string(k + 1), 2), " ", "0")
    name = id + "_teller_session"
    path = outdir + "/" + name + ".rpt"
    f {file}= path
    write(f, join(paged.lines, "\n") + "\n")

    append(manifest, { id: name,
                       file: name + ".rpt",
                       form: "teller",
                       variant: v,
                       pages: paged.pages,
                       physical_lines: count(paged.lines),
                       furniture_lines: paged.furniture,
                       families: fam,
                       sessions: sessions })
    k = k + 1
  end while

  t {file}= outdir + "/truth.json"
  write(t, encode({ generator: "tools/gen_discover_variants.bas",
                    seed: sd,
                    count: count_n,
                    form: "teller",
                    note: "PLANTED values, not values read back from the text. The variant corpus is these sources PLUS the first eight NN_branch_activity.rpt from the parent directory.",
                    sources: manifest }) + "\n")

  print ("wrote " + string(count_n) + " teller journals and truth.json to " + outdir)
end program
