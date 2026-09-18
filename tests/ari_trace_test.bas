' `ari.trace` -- WHICH SOURCE SPANS A SPECIFICATION CLAIMED, and what stayed
' unclaimed. Limitation C1 in docs/ari_limitations.md, and the thing two of the
' nine scoring measures in docs/ari_discover_design.md §8 were blocked on.
'
' SELF-CHECKING RATHER THAN GOLDEN, AND HERE THAT IS FORCED. Every defect this
' surface can have is a PLAUSIBLE SUBSTRING: an offset one character out yields
' an ordinary-looking token off the same line, a coverage fraction that counted
' whitespace yields a flattering percentage, and a collision rule that fires on
' the commonest correct specification yields a rate nobody can read. A golden
' would record any of them as expected and defend it.
'
' Every check states its own expected answer and prints `ok` or a MISMATCH
' naming both sides.

function check(label, got, want)
  if got = want then
    print "ok   " + label
  else
    print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
  end if
end function

function want_true(label, got)
  check(label, got, true)
end function

' Walk a dotted path into a parsed record. `rows[i].f` addresses a FRAME, whose
' columns are parallel arrays, so that step is not the same as an array of
' records and is handled where it occurs.
function at_path(root, path)
  parts = split(path, ".")
  cur = root
  i = 1
  while i < count(parts)
    p = parts[i]
    m = match(p, regex("^([A-Za-z_][A-Za-z_0-9]*)\\[([0-9]+)\\]$"))
    if is_unknown(m) then
      cur = cur[p]
      i = i + 1
      continue
    end if
    nm = m.groups[0]
    ix = number(m.groups[1])
    if nm = "rows" then
      ' rows[i].field  ->  cur.rows.field[i]
      fld = parts[i + 1]
      col = cur["rows"][fld]
      return col[ix]
    end if
    cur = cur[nm][ix]
    i = i + 1
  end while
  return cur
end function

function thin_spec()
  sp = []
  append(sp, "page:")
  append(sp, "    break: /^[0-9]{2}\\/[0-9]{2}\\/[0-9]{4} .*Page [0-9]+$/")
  append(sp, "    drop: 2")
  append(sp, "")
  append(sp, "section report starts(/^Branch: /):")
  append(sp, "    field branch: right of \"Branch:\" as integer")
  return join(sp, "\n")
end function

function full_spec()
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
  return join(sp, "\n")
end function

' Two fields deliberately reading THE SAME characters. Nothing else in this file
' produces a collision, so without this the collision measure would be asserted
' only at zero -- which is what a measure that never fires also reports.
function colliding_spec()
  sp = []
  append(sp, "page:")
  append(sp, "    break: /^[0-9]{2}\\/[0-9]{2}\\/[0-9]{4} .*Page [0-9]+$/")
  append(sp, "    drop: 2")
  append(sp, "")
  append(sp, "section report starts(/^Branch: /):")
  append(sp, "    section tellers repeats starts(/^Teller: /):")
  append(sp, "        section detail starts(/^GL[ ]+Tran #/) ends(/^[ ]*$/):")
  append(sp, "            rows:")
  append(sp, "                field gl: columns 0-24")
  append(sp, "                field gl_again: columns 10-30")
  return join(sp, "\n")
end function

program main(args)
  load ari

  f{file} = "examples/fixtures/ari/teller_totals.rpt"
  raw = read_lines(f)
  report = join(raw, "\n")

  full = full_spec()
  t = ari.trace(report, full)
  want_true("trace parses the fixture", t.ok)

  print ""
  print "TIER parity (one walk, two callers)"
  ' A trace that described a DIFFERENT program from the one `parse` runs would
  ' be worse than no trace at all, so the two entry points share `_parse_with`
  ' and this asserts they cannot have drifted.
  p = ari.parse(report, full)
  check("trace value = parse value", string(t.value), string(p.value))
  check("trace diagnostics = parse diagnostics", string(t.diagnostics), string(p.diagnostics))
  check("trace lines = parse lines", t.lines, p.lines)

  print ""
  print "TIER line numbering (the claim points into the FILE)"
  ' What this tier does and does NOT establish, because the distinction is the
  ' one this project keeps relearning: `text` is cut from the same grid row the
  ' offset indexes, so comparing the two would be the library agreeing with
  ' itself. What is independent here is the LINE NUMBER -- the grid carries a
  ' physical line through the furniture pass, and this asserts that number still
  ' addresses the same bytes in the file. The OFFSET is established by the
  ' anchor tier (against the specification) and the value tier (against the
  ' parsed value), neither of which can be satisfied by self-consistency.
  bad_text = 0
  bad_line = 0
  bad_extent = 0
  for each c in t.claims
    if c.line < 1 then
      bad_line = bad_line + 1
      continue
    end if
    if c.line > count(raw) then
      bad_line = bad_line + 1
      continue
    end if
    src = raw[c.line - 1]
    if c.start < 0 then
      bad_extent = bad_extent + 1
      continue
    end if
    if c.start + c.length > len(src) then
      bad_extent = bad_extent + 1
      continue
    end if
    if mid(src, c.start, c.length) != c.text then
      bad_text = bad_text + 1
      if bad_text < 4 then
        print "     first: " + c.path + " line " + c.line + " [" + c.start + "," + c.length + "] claimed " + quote(c.text) + " file has " + quote(mid(src, c.start, c.length))
      end if
    end if
  end for
  print "     claims: " + count(t.claims)
  check("every claim's line addresses the same bytes in the file", bad_text, 0)
  check("every claim names a real source line", bad_line, 0)
  check("every claim's extent lies inside its line", bad_extent, 0)

  print ""
  print "TIER value oracle (the span is where the VALUE came from)"
  ' Pointing at the right LINE is not the claim being made. The claimed span,
  ' handed back to the library on its own, must produce the value the whole
  ' parse produced -- which is what separates a real offset from a plausible
  ' neighbouring one.
  checked = 0
  disagreed = 0
  for each c in t.claims
    if c.kind != "field" then
      continue
    end if
    v = at_path(t.value, c.path)
    if is_unknown(v) then
      continue
    end if
    if c.type = "" then
      checked = checked + 1
      if trim(c.text) != v then
        disagreed = disagreed + 1
        if disagreed < 4 then
          print "     first: " + c.path + " span " + quote(trim(c.text)) + " value " + quote(string(v))
        end if
      end if
      continue
    end if
    one = "section r:" + "\n" + "    field v: first " + c.type
    rr = ari.parse(c.text, one)
    checked = checked + 1
    if not rr.ok then
      disagreed = disagreed + 1
      continue
    end if
    if rr.value.v != v then
      disagreed = disagreed + 1
      if disagreed < 4 then
        print "     first: " + c.path + " reparsed " + string(rr.value.v) + " parsed " + string(v)
      end if
    end if
  end for
  print "     field claims re-read from their own span: " + checked
  want_true("enough claims to measure", checked > 40)
  check("re-reading the claimed span gives the parsed value", disagreed, 0)

  print ""
  print "TIER arithmetic (claimed + unclaimed = content)"
  ' Free, and it catches what a percentage cannot: a character counted twice, or
  ' one that fell out of both sides.
  un = 0
  for each u in t.unclaimed
    i = 0
    while i < u.length
      ch = mid(u.text, i, 1)
      if ch != " " then
        un = un + 1
      end if
      i = i + 1
    end while
  end for
  print "     content " + t.content_chars + " = claimed " + t.claimed_chars + " + unclaimed " + un
  check("no character is counted twice or lost", t.claimed_chars + un, t.content_chars)
  want_true("coverage is the claimed share", t.content_coverage = t.claimed_chars / t.content_chars)

  print ""
  print "TIER whitespace is not content"
  ' A print image is mostly column padding. Counted as content it would be the
  ' denominator of every coverage figure, and no specification could ever reach
  ' a number a reader would believe -- flattering in the other direction and
  ' just as useless. Asserted as a RATIO of the fixture's own bytes.
  allchars = 0
  clean = ari.clean_grid(report, full)
  for each ln in clean
    allchars = allchars + len(ln)
  end for
  blanks = 0
  for each ln in clean
    i = 0
    while i < len(ln)
      ch = mid(ln, i, 1)
      if ch = " " then
        blanks = blanks + 1
      end if
      i = i + 1
    end while
  end for
  print "     grid characters " + allchars + " = content " + t.content_chars + " + blank " + blanks
  check("every character is content or blank, and blanks are not content",
        t.content_chars + blanks, allchars)
  want_true("the fixture really is half padding", blanks * 3 > allchars)

  print ""
  print "TIER difference (coverage measures the SPECIFICATION)"
  ' A coverage number that reads the same for a spec taking one field and a spec
  ' taking twenty is measuring the report, not the specification. The margin is
  ' asserted, not the value: the value is a property of this fixture.
  thin = ari.trace(report, thin_spec())
  print "     one field " + thin.content_coverage + "   twenty " + t.content_coverage
  want_true("the fuller specification explains more", t.content_coverage > thin.content_coverage)
  want_true("and by a wide margin, not a rounding", t.content_coverage > thin.content_coverage * 4)
  check("both read the same report", thin.content_chars, t.content_chars)

  print ""
  print "TIER anchors (a literal the spec NAMES is explained)"
  ' The anchors are the most spec-relevant characters on the page. Left out of
  ' the claims they would sit in the unclaimed list, and coverage would be
  ' depressed by exactly the literals the author wrote.
  anchored = false
  for each c in t.claims
    if c.kind = "anchor" then
      if c.text = "Beginning Cash" then
        anchored = true
      end if
    end if
  end for
  want_true("`Beginning Cash`, which the spec names, is claimed", anchored)

  ' AN OFFSET ORACLE THAT IS NOT THE LIBRARY AGREEING WITH ITSELF: an anchor
  ' claim must reproduce, character for character, a literal written in its own
  ' rule. One column out and `Beginning Cash` comes back as `eginning Cash ` --
  ' an ordinary-looking string that the file check above accepts.
  lits = 0
  wrong = 0
  for each c in t.claims
    if c.kind != "anchor" then
      continue
    end if
    ms = match_all(c.rule, regex("\"([^\"]*)\""))
    if count(ms) = 0 then
      continue
    end if
    ok_one = false
    for each m in ms
      if m.groups[0] = c.text then
        ok_one = true
      end if
    end for
    lits = lits + 1
    if not ok_one then
      wrong = wrong + 1
      if wrong < 4 then
        print "     first: rule " + quote(c.rule) + " claimed " + quote(c.text)
      end if
    end if
  end for
  print "     literal anchors checked against their own rule: " + lits
  want_true("enough anchors to measure", lits > 20)
  check("every anchor claim IS the literal its rule names", wrong, 0)
  ' THE CONTROL, and it is on a line that IS claimed: `Summary` sits at the end
  ' of every Teller line, whose name, number and heading are all claimed, and no
  ' rule in the specification names that word. Without this, "claimed" would be
  ' satisfied by claiming everything.
  stray = false
  for each u in t.unclaimed
    if u.text = "Summary" then
      stray = true
    end if
  end for
  want_true("a word on a claimed line that no rule names is unclaimed", stray)

  print ""
  print "TIER furniture (a stripped page header is neither claimed nor unclaimed)"
  ' The measure is over non-furniture text by definition. A page header counted
  ' as unclaimed would penalise every specification for the printer's work.
  hdr = 0
  for each c in t.claims
    if contains(raw[c.line - 1], regex("Page [0-9]+$")) then
      hdr = hdr + 1
    end if
  end for
  for each u in t.unclaimed
    if contains(raw[u.line - 1], regex("Page [0-9]+$")) then
      hdr = hdr + 1
    end if
  end for
  check("no page-furniture line appears in the report", hdr, 0)
  ' The control: those lines really are in the file, so the zero above is a
  ' fact about the furniture pass and not about an empty fixture.
  infile = 0
  for each ln in raw
    if contains(ln, regex("Page [0-9]+$")) then
      infile = infile + 1
    end if
  end for
  want_true("and the fixture really does carry page headers", infile > 1)

  print ""
  print "TIER collisions (a span explained twice)"
  check("an ordinary specification reports none", count(t.collisions), 0)
  check("and its rate is zero", t.collision_rate, 0)
  cc = ari.trace(report, colliding_spec())
  want_true("two fields reading one span report a collision", count(cc.collisions) > 0)
  want_true("and the rate is above zero", cc.collision_rate > 0)
  named = false
  for each c in cc.collisions
    if contains(c.paths[0], "gl") then
      if contains(c.paths[1], "gl_again") then
        named = true
      end if
    end if
  end for
  want_true("the collision names both rules", named)

  print ""
  print "TIER definitions travel with the numbers"
  ' A bare fraction in [0,1] printed beside a specification reads as a grade.
  ' Neither of these is one, and saying so is part of the answer.
  want_true("content_coverage carries its definition", len(t.content_coverage_is) > 40)
  want_true("collision_rate carries its definition", len(t.collision_rate_is) > 40)
  want_true("the coverage definition says blanks are excluded", contains(t.content_coverage_is, "non-blank"))
  want_true("the collision definition says which overlaps do not count", contains(t.collision_rate_is, "deliberately"))

  print ""
  print "TIER a span that covers nothing is not a span"
  ' `columns 90-95` on a 79-column line yields the EMPTY STRING -- a value, not
  ' an `unknown` -- and its offset is then past the end of the line it names.
  ' Recorded, it would be a claim pointing at a place a reader cannot look.
  zsp = "section r starts(/^A/):" + "\n" + "    field wide: columns 90-95" + "\n" + "    field narrow: columns 2-5"
  z = ari.trace("ABCDEFG", zsp)
  zp = ari.parse("ABCDEFG", zsp)
  check("the empty span still produces a value", zp.value.wide, "")
  paths = []
  for each c in z.claims
    append(paths, c.path)
  end for
  want_true("and claims nothing", not contains(paths, "r.wide"))
  ' THE CONTROL: the field beside it, which covers real characters, IS claimed --
  ' or "claims nothing" would be satisfied by a spec that claimed nothing at all.
  want_true("while the field beside it is", contains(paths, "r.narrow"))

  print ""
  print "TIER a failed parse still answers"
  bad = ari.trace(report, "section nope starts(/^NOTHING HERE/):" + "\n" + "    field x: columns 0-1")
  check("ok is false", bad.ok, false)
  check("claims are empty rather than absent", count(bad.claims), 0)
  want_true("coverage is unknown, never 0", is_unknown(bad.content_coverage))
  want_true("collision rate is unknown, never 0", is_unknown(bad.collision_rate))
end program
