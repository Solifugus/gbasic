' Generate a NULL corpus for ARI Discover: report-SHAPED text with no
' recoverable structure in it.
' (docs/ari_discover_design.md §15.5 and §17; see the MANIFEST.)
'
'   gbasic tools/gen_discover_null.bas [outdir] [count] [seed]
'
'     outdir   directory to write into    (default examples/fixtures/ari_discover/null)
'     count    reports to emit            (default 12)
'     seed     RNG seed                   (default 90210)
'
' WHY A NULL CORPUS EXISTS AT ALL
'
' Inference is a SEARCH, and a search always returns a winner. This project has
' measured that once already and it was the most expensive result in the tree:
' examples/automation_lab recipe 1 ran the same decomposition over a population
' with a real 45% collapse planted in a known cell and over one with NOTHING in
' it but noise, and the output COULD NOT TELL THEM APART -- both produced a
' confident three-level causal chain, both declined 1.8%, and the top-region
' share was 82.6% against 80.3%.
'
' ARI Discover is the same shape of machine. It proposes line families, anchors
' and field rules from the corpus and scores them. Any token that happens to
' recur is a candidate anchor; on a small corpus at `minimum_support: 0.80`,
' coincidences clear. A scorecard full of high numbers on a corpus that HAS
' structure is therefore not evidence that discovery found the structure -- it
' is consistent with a tool that always finds something.
'
' The only thing that separates those two is running it where the right answer
' is NOTHING.
'
' WHAT MAKES THIS A FAIR NULL, which is the whole difficulty. The temptation is
' to emit random letters, and that measures nothing: discovery would reject it
' for reasons -- no money, no dates, no report-like density -- that have
' nothing to do with structure. So this corpus is DELIBERATELY INDISTINGUISHABLE
' FROM THE REAL ONE AT THE TOKEN LEVEL:
'
'   * the same token kinds, drawn from the same vocabularies -- account-shaped
'     ids, surnames, money with the same notations, dates in the same dialects;
'   * comparable line lengths and visual density;
'   * plausible indentation.
'
' and it differs in exactly one respect, which is the one under test:
'
'   * NO STABLE STRUCTURE. Token order, token count and indentation are drawn
'     per line, so no two lines share a signature except by accident. No
'     literal recurs at a stable position. There is no page furniture, no
'     heading, no total, and no repeating family.
'
' THE EXPECTED RESULT IS A REFUSAL, NOT A LOW SCORE. §14 already names
' "insufficient variation to distinguish constants from variables" and "no
' stable anchors" as outcomes; this corpus is what turns those from a listed
' possibility into a measured rate. The acceptance criterion in §17 is that no
' specification is proposed at or above the default confidence, across every
' source here -- and the FALSE-POSITIVE RATE over this corpus is a number to
' report, not a box to tick.
'
' DETERMINISM. A pure function of (outdir, count, seed).

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

function zero2(n)
  return replace(pad_left(string(n), 2), " ", "0")
end function

' The SAME three notations the real corpus uses, drawn per line rather than
' per source. In the real corpus the notation is a declared property of the
' report; here it is noise, which is one of the things that makes this text
' structureless without making it look unlike a report.
function money_token()
  cents = 2500 + random_int(0, 480000)
  neg = random_int(0, 5) = 0
  whole = floor(cents / 100)
  frac = cents - whole * 100
  body = group_digits(whole) + "." + zero2(frac)
  if not neg then
    return body
  end if
  st = random_int(0, 2)
  if st = 0 then
    return body + "-"
  end if
  if st = 1 then
    return "(" + body + ")"
  end if
  return "-" + body
end function

function date_token()
  m = 1 + random_int(0, 11)
  d = 13 + random_int(0, 15)
  if random_int(0, 1) = 0 then
    names = [ "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" ]
    return zero2(d) + "-" + names[m - 1] + "-2026"
  end if
  return zero2(m) + "/" + zero2(d) + "/2026"
end function

function id_token()
  return "001" + pad_left(string(40000 + random_int(0, 9999)), 5)
end function

function name_token()
  sn = [ "MERCER", "OKAFOR", "LINDQVIST", "ROSSI", "SANTOS", "NAKAMURA",
         "ABIODUN", "PETROVA", "CHAUDHRY", "WHITFIELD", "DELACROIX",
         "ANDERSSON", "MWANGI", "KOVACS", "REYES", "THORNBURY" ]
  fn = [ "ALICE", "BRENDAN", "EVA", "CHIARA", "DIEGO", "YUKI", "FEMI",
         "IRINA", "NADIA", "MARGARET", "PHILIPPE", "GUNNAR", "ESTHER" ]
  return sn[random_int(0, count(sn) - 1)] + ", " + fn[random_int(0, count(fn) - 1)]
end function

' A word drawn from a LARGE pool, so no literal recurs often enough to look
' like an anchor. The real corpus's anchors ("BRANCH", "REMARKS:", "ACCT")
' recur on every page BY DESIGN; here nothing does.
function word_token()
  w = [ "POSTED", "CLEARED", "HELD", "REVIEW", "ADJUST", "MEMO", "ITEM",
        "SUSPENSE", "TRANSIT", "DRAFT", "RETURN", "CHARGE", "CREDIT",
        "TRANSFER", "REVERSAL", "CORRECTION", "SETTLEMENT", "ADVICE",
        "LEDGER", "JOURNAL", "BATCH", "CYCLE", "PERIOD", "SEGMENT",
        "CLASS", "SERIES", "REGION", "OFFICE", "WINDOW", "TERMINAL" ]
  return w[random_int(0, count(w) - 1)]
end function

function one_token(k)
  if k = 0 then
    return id_token()
  end if
  if k = 1 then
    return name_token()
  end if
  if k = 2 then
    return date_token()
  end if
  if k = 3 then
    return money_token()
  end if
  return word_token()
end function

program main( args )
  outdir = "examples/fixtures/ari_discover/null"
  count_n = 12
  sd = 90210

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
  manifest = []

  k = 0
  while k < count_n
    nlines = 40 + random_int(0, 40)
    lines = []
    i = 0
    while i < nlines
      ' A blank line now and then, at no regular interval -- a report has
      ' blanks, and emitting none would be a giveaway that has nothing to do
      ' with structure.
      if random_int(0, 9) = 0 then
        append(lines, "")
      else
        ntok = 2 + random_int(0, 3)
        parts = []
        j = 0
        while j < ntok
          append(parts, one_token(random_int(0, 4)))
          j = j + 1
        end while
        ' Gaps and indentation drawn per line, so no two lines share a
        ' column layout except by accident.
        body = ""
        j = 0
        while j < count(parts)
          if j > 0 then
            body = body + repeat(" ", 2 + random_int(0, 6))
          end if
          body = body + parts[j]
          j = j + 1
        end while
        append(lines, repeat(" ", random_int(0, 8)) + body)
      end if
      i = i + 1
    end while

    id = replace(pad_left(string(k + 1), 2), " ", "0")
    name = "null_" + id + "_noise"
    f {file}= outdir + "/" + name + ".rpt"
    write(f, join(lines, "\n") + "\n")
    append(manifest, { id: name, file: name + ".rpt",
                       physical_lines: count(lines) })
    k = k + 1
  end while

  t {file}= outdir + "/truth.json"
  write(t, encode({ generator: "tools/gen_discover_null.bas",
                    seed: sd,
                    count: count_n,
                    expected: { families: 0, fields: 0, furniture_lines: 0,
                                sections: 0 },
                    claim: "There is no structure here to find. A specification proposed at or above the default confidence over any of these sources is a FALSE POSITIVE, and the rate over the corpus is the number to report.",
                    sources: manifest }) + "\n")

  print ("wrote " + string(count_n) + " null reports and truth.json to " + outdir)
end program
