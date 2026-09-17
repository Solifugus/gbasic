' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' ari_discover -- PHASE 0: profiling. See docs/ari_discover_design.md.
'
' WHAT PHASE 0 IS AND IS NOT. It measures what repeats, varies and collides in
' a corpus of print-image reports, and it PROPOSES NOTHING. There is no
' specification generation here, deliberately: design principle 1 is
' "measurement before interpretation", and a profiling layer that also guessed
' would make the guess impossible to evaluate separately from the measurement
' it rests on.
'
' THE ARCHITECTURAL RULE, from §2: this library depends on `ari` and `ari`
' never depends on this one. The deterministic parser must stay free of
' clustering, of inference and of any model, so a caller with a known
' specification pays none of it.
'
' TWO INDEX SPACES, NEVER MIXED (§6.1). `ari` locates in CODEPOINTS -- its
' `columns` rule, every recognizer, `len` and `mid` -- so anything that could
' become part of a generated rule is measured in codepoints. Provenance back
' to the file is measured in BYTES, because that is what maps to what a person
' opens. Every position field says which: `cp_` or `byte_`, never a bare
' `offset`, `start` or `position`.
'
' THIS IS THE DEFECT finio PHASE 0 SHIPPED, one library over: it accumulated
' offsets with `len`, sliced with `mid`, called the result `byte_offset`, and
' its own fixture could not see it because the fixture was pure ASCII. It took
' a foreign corpus carrying one 95-byte, 94-codepoint record to expose it. The
' two spaces coincide on ASCII, which is exactly why the rule has to be a rule
' rather than something noticed later.
'
' RECOGNIZERS ARE SHARED, NOT COPIED (§20). `ari.money_patterns()` and
' `ari.date_patterns()` are the same tables `_money_in` and `_date_in` consume.
' A second copy of "what money looks like" would drift, and the failure is
' silent: a profile reporting a DATE in a column the engine answers
' `no-date-found` for.

library ari_discover

load ari from "ari.bas"

' --- §5.2 options ---------------------------------------------------------
'
' The record is VALIDATED BY NAME and an unrecognised field is refused. A
' misspelled `minimum_suport` that were silently ignored would leave the caller
' believing they raised a threshold they did not, and the run that followed
' would look like a successful one. This is the rule webserver.listen,
' odbc.connect, discovery.scan and finio.open_text already follow.

function default_options()
    return { minimum_support: 0.80,
             minimum_confidence: 0.70,
             maximum_section_depth: 4,
             allow_fixed_columns: false,
             redact_examples: false,
             llm: nothing }
end function

function option_names()
    return keys(default_options())
end function

' `options = nothing` rather than a record default, because gBASIC default
' parameter values are LITERALS. `nothing` is "the caller did not supply one";
' `unknown` would be the answer to a question nobody asked, and a caller
' reading the field back must be able to tell those apart.
function _options(given)
    if is_nothing(given) then
        return default_options()
    end if
    if type(given) != "record" then
        error ("ari_discover: options must be a record, not a " + type(given))
    end if
    known = option_names()
    for each k in keys(given)
        if not contains(known, k) then
            error ("ari_discover: '" + string(k) + "' is not an option -- "
                   + "the options are " + join(known, ", "))
        end if
    end for
    out = default_options()
    for each k in keys(given)
        out[k] = given[k]
    end for
    return out
end function

' --- §6.2 token kinds -----------------------------------------------------
'
' `page_number` and `separator` are NOT in this list and that is deliberate.
' Both are conclusions about a line's ROLE, not about a token's shape, and a
' recognizer that decided them here would be doing inference inside the
' measurement layer -- the thing §7 Phase 3 warns against when it says a line
' is not furniture merely because it repeats.
function token_kinds()
    return [ "money", "date", "number", "identifier", "word" ]
end function

' --- typed spans within one line -----------------------------------------
'
' Returns spans ordered by position, each carrying BOTH index spaces:
'
'   { kind, text, cp_start, cp_length, byte_start, byte_length,
'     alternatives, needs_dialect }
'
' `cp_*` are relative to the LINE and are what a generated `columns` rule would
' contain. `byte_*` are relative to the line too; the caller adds the line's own
' byte offset to reach the file. Both are returned because neither can be
' derived from the other without the text, and deriving it at every call site
' is how the two spaces get mixed.
'
' AMBIGUITY IS RECORDED, NOT RESOLVED (design principle 5). `20260916` is
' plausibly a date, an identifier and an integer; the alternatives travel with
' the span and the selected kind is the most specific recognizer that claimed
' it. A premature choice here cannot be revisited by anything downstream.

function _byte_index_of(text, cp)
    ' The codepoint offset `cp` converted to a byte offset within `text`.
    ' Done by measuring the prefix, which is the only way that is correct for
    ' multi-byte content -- and the reason this function exists at all rather
    ' than the two numbers being assumed equal.
    return byte_count(mid(text, 0, cp))
end function

function _claims(text, patterns, kind)
    out = []
    for each p in patterns
        ms = match_all(text, regex(p.re))
        for each m in ms
            k = kind
            nd = false
            if has(p, "needs_dialect") then
                nd = p.needs_dialect
            end if
            append(out, { kind: k, text: m.text,
                          cp_start: m.start, cp_length: m.length,
                          alternatives: [],
                          needs_dialect: nd })
        end for
    end for
    return out
end function

function _overlaps(a, b)
    if a.cp_start >= b.cp_start + b.cp_length then
        return false
    end if
    if b.cp_start >= a.cp_start + a.cp_length then
        return false
    end if
    return true
end function

function spans(line_text)
    ' MOST SPECIFIC FIRST, REJECTING OVERLAPS. This is `_money_in`'s own rule
    ' and it is here for the reason its comment gives: in "... $6,000.25-" a
    ' specific pattern claims "$6,000.25-" and a generic one claims "6,000.25"
    ' INSIDE it, further right -- so a naive rightmost-wins rule drops the sign
    ' and turns -6000.25 into 6000.25.
    candidates = []
    for each c in _claims(line_text, ari.money_patterns(), "money")
        append(candidates, c)
    end for
    for each c in _claims(line_text, ari.date_patterns(), "date")
        append(candidates, c)
    end for
    ' An identifier is a run of digits long enough not to be an ordinary
    ' number, or a mixed alphanumeric token. Deliberately checked AFTER money
    ' and date, so "16" inside "16-OCT-2026" is never a separate identifier.
    for each c in _claims(line_text, [ { re: "[0-9]{5,}" } ], "identifier")
        append(candidates, c)
    end for
    for each c in _claims(line_text, [ { re: "-?[0-9]+" } ], "number")
        append(candidates, c)
    end for
    for each c in _claims(line_text, [ { re: "[A-Za-z][A-Za-z'.#/-]*" } ], "word")
        append(candidates, c)
    end for

    chosen = []
    for each c in candidates
        clash = false
        for each k in chosen
            if _overlaps(c, k) then
                clash = true
            end if
        end for
        if not clash then
            append(chosen, c)
        else
            ' The loser is recorded on the winner as an alternative, because
            ' §6.2 says ambiguity is recorded rather than discarded -- and the
            ' span that lost is exactly the competing hypothesis §10 will ask
            ' a person about.
            i = 0
            while i < count(chosen)
                if _overlaps(c, chosen[i]) then
                    w = chosen[i]
                    if not contains(w.alternatives, c.kind) then
                        append(w.alternatives, c.kind)
                    end if
                    chosen[i] = w
                end if
                i = i + 1
            end while
        end if
    end for

    ' Sort by position and attach the byte space.
    out = []
    taken = []
    n = count(chosen)
    j = 0
    while j < n
        best = -1
        k = 0
        while k < n
            if not contains(taken, k) then
                if best < 0 then
                    best = k
                else
                    if chosen[k].cp_start < chosen[best].cp_start then
                        best = k
                    end if
                end if
            end if
            k = k + 1
        end while
        append(taken, best)
        c = chosen[best]
        bs = _byte_index_of(line_text, c.cp_start)
        be = _byte_index_of(line_text, c.cp_start + c.cp_length)
        append(out, { kind: c.kind, text: c.text,
                      cp_start: c.cp_start, cp_length: c.cp_length,
                      byte_start: bs, byte_length: be - bs,
                      alternatives: c.alternatives,
                      needs_dialect: c.needs_dialect })
        j = j + 1
    end while
    return out
end function

' --- §6.3 line signature --------------------------------------------------
'
' Variable spans become typed placeholders; stable literals survive as
' themselves. What the signature KEEPS is the whole design: token order, and
' whether a typed value is first or last. What it DROPS is exact column
' positions -- "exact columns are evidence, not identity" -- because a
' signature keyed to columns makes every one of the corpus's three indent
' variants a different family, which is precisely the drift the tool exists to
' survive.
'
' A WORD IS KEPT VERBATIM and a typed span is not. That asymmetry is the point:
' `BRANCH TOTAL` must stay recognisable as a literal so it can be proposed as
' an anchor, while `1,245.00` must not, or every detail row is its own family.

' A RULE LINE MAY HAVE GAPS IN IT, and the first version of this required one
' unbroken run. A column rule under a table heading is several runs separated
' by spaces -- `----------  ----------------------   ----------` -- so it
' matched nothing, fell through to the token scan which found no words or
' numbers in it either, and every such line was profiled as `<OTHER>`. Four per
' source, silently, in a category whose name says the tool does not know.
function _is_rule(t)
    if len(t) < 4 then
        return false
    end if
    if is_unknown(match(t, regex("^[-=_*. ]+$"))) then
        return false
    end if
    ' At least four punctuation characters, so a line of spaces and one dash is
    ' not a rule.
    n = 0
    i = 0
    while i < len(t)
        if mid(t, i, 1) != " " then
            n = n + 1
        end if
        i = i + 1
    end while
    return n >= 4
end function

function signature(line_text)
    t = trim(line_text)
    if len(t) = 0 then
        return "<BLANK>"
    end if
    ' A rule line is its own signature: a run of one punctuation character is
    ' furniture-shaped and would otherwise become a long meaningless literal.
    if _is_rule(t) then
        return "<RULE>"
    end if
    parts = []
    sp = spans(line_text)
    for each s in sp
        if s.kind = "word" then
            append(parts, upper(s.text))
        else
            append(parts, "<" + upper(s.kind) + ">")
        end if
    end for
    if count(parts) = 0 then
        return "<OTHER>"
    end if
    return join(parts, " ")
end function

' The signature with LITERALS BLANKED TOO -- the shape alone. Furniture
' detection needs this: a page header differs from page to page only in its
' page number and run stamp, so its literal signature is stable, but a HEADING
' whose wording drifts across sources is only recognisable at this level.
function shape(line_text)
    t = trim(line_text)
    if len(t) = 0 then
        return "<BLANK>"
    end if
    if _is_rule(t) then
        return "<RULE>"
    end if
    parts = []
    for each s in spans(line_text)
        append(parts, "<" + upper(s.kind) + ">")
    end for
    if count(parts) = 0 then
        return "<OTHER>"
    end if
    return join(parts, " ")
end function

' --- §6.1 source grid -----------------------------------------------------
'
' No preprocessing step may destroy the mapping back to the original bytes, so
' the grid carries the line's own byte offset into the file and its byte
' length, alongside the codepoint measures a rule would use.
'
' A FORM FEED IS A LINE, not a character to strip. It is the strongest page
' evidence a print-image report carries, and removing it in normalisation
' would throw away the one signal that makes pagination unambiguous.

function grid(source_id, report_text)
    rows = []
    lines = split(report_text, "\n")
    ' A trailing newline produces a final empty element which is not a line.
    n = count(lines)
    if n > 0 then
        if lines[n - 1] = "" then
            n = n - 1
        end if
    end if
    byte_at_line = 0
    page = 1
    i = 0
    while i < n
        raw = lines[i]
        ff = contains(raw, chr(12))
        body = replace(raw, chr(12), "")
        t = trim(body)
        ind = 0
        while ind < len(body)
            if mid(body, ind, 1) != " " then
                break
            end if
            ind = ind + 1
        end while
        append(rows, { source_id: source_id,
                       physical_line: i + 1,
                       page_guess: page,
                       text: body,
                       form_feed: ff,
                       byte_start: byte_at_line,
                       byte_length: byte_count(raw),
                       cp_length: len(body),
                       indent: ind,
                       trimmed_length: len(t),
                       blank: len(t) = 0 })
        if ff then
            page = page + 1
        end if
        byte_at_line = byte_at_line + byte_count(raw) + 1
        i = i + 1
    end while
    return rows
end function

' --- §7 Phase 3: page furniture -------------------------------------------
'
' "A line is not furniture merely because it repeats. A repeated section
' heading inside the report body must remain available to section inference.
' Furniture classification therefore requires POSITIONAL evidence in addition
' to textual recurrence."
'
' That rule decides the whole algorithm. Furniture is found by PERIOD: lines
' whose shape recurs at a constant interval, at a stable offset within that
' interval. A heading that repeats irregularly through the body fails the
' interval test and survives into section inference, which is what must happen.
'
' TWO CASES, AND BOTH ARE IN THE CORPUS. With form feeds the period is given.
' Without them it has to be inferred, and the honest way is to measure the gaps
' between recurrences of a stable shape and take the modal one -- then REQUIRE
' that most gaps agree, rather than accepting whatever the mode happened to be.

function _modal_gap(positions)
    if count(positions) < 2 then
        return { gap: 0, agreement: 0 }
    end if
    gaps = []
    i = 1
    while i < count(positions)
        append(gaps, positions[i] - positions[i - 1])
        i = i + 1
    end while
    tally = {}
    for each g in gaps
        k = string(g)
        if has(tally, k) then
            tally[k] = tally[k] + 1
        else
            tally[k] = 1
        end if
    end for
    best = 0
    bestn = 0
    for each k in keys(tally)
        if tally[k] > bestn then
            bestn = tally[k]
            best = number(k)
        end if
    end for
    return { gap: best, agreement: bestn / count(gaps) }
end function

' WHERE EACH PAGE BEGINS. Reported as page STARTS rather than as a period, and
' that is not a presentation choice: a two-page report has exactly ONE page
' break, so a period needs two gaps to infer and cannot be had at all. The
' first draft required two and found nothing on more than half the corpus --
' reports short enough to be two pages, which is most real ones.
'
' WITH FORM FEEDS NOTHING IS INFERRED: the breaks are stated.
'
' WITHOUT THEM, THE EVIDENCE MUST BE A BLOCK. The second draft accepted a
' single line shape recurring at a constant interval, and MEASURED over the
' corpus that invented pages in 14 of 18 single-page sources -- claiming up to
' 24 furniture lines in a report whose header appears exactly once. It also
' claimed 18 furniture lines across the NULL corpus, where the right answer is
' none.
'
' The cause is that a single recurring shape is not evidence of a page, which
' is what §7 Phase 3 says in as many words: "a line is not furniture merely
' because it repeats". A page header is a BLOCK -- a title, an organisation
' line, a rule, a blank -- and requiring at least two consecutive agreeing
' shapes at the same offset is what separates it from a detail row that happens
' to land on a regular interval.
function _shapes_of(rows)
    out = []
    for each r in rows
        append(out, shape(r.text))
    end for
    return out
end function

' The WORD tokens of a line, joined. §7 Phase 3 asks for "similarity across
' pages, and variable slots such as page number or run date" -- so a furniture
' line is one whose LITERALS are stable while its typed values may vary. A page
' header differs from page to page only in its page number and run stamp, both
' of which are typed spans and neither of which is a word.
'
' THIS IS WHAT STOPS A PAGE BREAK'S COINCIDENCE FROM BECOMING A HEADER.
' MEASURED: with shape agreement alone, two sources claimed 17 furniture lines
' where 9 were planted -- the break happened to fall just before a branch
' heading, so page 2 opened with the same eight SHAPES as page 1 and the whole
' window agreed. The branch NAME differs between them, which is a word, so the
' block trims back to the four lines that are really the header.
function _words_of(line_text)
    parts = []
    for each sp in spans(line_text)
        if sp.kind = "word" then
            append(parts, upper(sp.text))
        end if
    end for
    return join(parts, " ")
end function

function _words_at(rows, n, starts, off)
    out = []
    for each st in starts
        i = st - 1 + off
        if i < n then
            append(out, _words_of(rows[i].text))
        end if
    end for
    return out
end function

' How many consecutive leading line-offsets agree across the pages implied by
' (period, base). The answer is the height of the repeated block.
' THE BLOCK IS PAGE ONE'S LEADING LINES, CONFIRMED BY THE OTHER PAGES.
'
' That direction matters and the first version had it backwards: it took the
' DOMINANT shape at each offset across all pages, which lets a block be defined
' by pages 2..5 while page 1 disagrees. MEASURED, that is exactly what happened
' -- a source with one real page locked onto a 14-line period whose "header"
' was a detail-row shape agreeing on four of five imagined pages, with the
' actual first line the odd one out. A page header appears on page one; if
' page one does not have it, it is not a header.
'
' AND IT SUBSUMES A GUARD THAT USED TO BE HERE. Before this rule, the block
' also had to contain a line that was neither blank nor a rule, because a
' "block" of two blanks or two rules agrees for free and invented pages in two
' sources. MEASURED after the page-one rule landed: removing that guard changes
' NO answer anywhere in the corpus or the null corpus -- page one's own shapes
' now decide the block, and a report whose first line is blank is rejected at
' offset 0 regardless. The guard was kept only as far as offset 0, where it is
' still doing work; the rest was dead code, and dead code is how a retired
' rule comes back.
function _block_height(sh, n, base, period, support)
    pages = []
    ln = base
    while ln < n
        append(pages, ln)
        ln = ln + period
    end while
    if count(pages) < 2 then
        return { height: 0, pages: count(pages) }
    end if
    h = 0
    off = 0
    while off < 8
        if base + off >= n then
            break
        end if
        want = sh[base + off]
        seen = 0
        agree = 0
        for each st in pages
            i = st + off
            if i < n then
                seen = seen + 1
                if sh[i] = want then
                    agree = agree + 1
                end if
            end if
        end for
        if seen < 2 then
            break
        end if
        if agree / seen < support then
            break
        end if
        ' A BLOCK THAT BEGINS WITH A BLANK IS NOT A HEADER: blank lines agree
        ' with each other for free.
        if off = 0 then
            if want = "<BLANK>" then
                return { height: 0, pages: count(pages) }
            end if
        end if
        h = h + 1
        off = off + 1
    end while
    return { height: h, pages: count(pages) }
end function

function page_starts(rows, options = nothing)
    o = _options(options)
    ffs = []
    for each r in rows
        if r.form_feed then
            append(ffs, r.physical_line)
        end if
    end for
    if count(ffs) >= 1 then
        starts = [ 1 ]
        for each ln in ffs
            if ln + 1 <= count(rows) then
                append(starts, ln + 1)
            end if
        end for
        return { starts: starts, evidence: "form feeds", height: 0 }
    end if

    sh = _shapes_of(rows)
    n = count(sh)
    best_h = 0
    best_p = 0
    best_pages = 0
    period = 10
    ' PAGINATION STARTS AT THE TOP OF THE FILE. Only base 0 is considered, and
    ' that one constraint is what separates a page header from a SECTION block.
    '
    ' MEASURED: with any base allowed, the search locks onto the branch block --
    ' heading, blank, column heading, rule, details, total -- which genuinely
    ' repeats at a nearly constant interval and is a perfectly good repeating
    ' block. It claimed up to 24 furniture lines on single-page sources whose
    ' header appears exactly once. A page-image report begins on page one; a
    ' section does not begin at the top of the file except by accident, and
    ' when it does the header block above it is still at offset 0.
    ' The period may be nearly the whole document: a 55-line report with a
    ' 50-line page has two pages, the second five lines long. The first draft
    ' searched only to n/2 -- which assumes two FULL pages -- and so found no
    ' period at all on every source whose second page is a short tail, which is
    ' most of them. A period that leaves too little to compare is rejected by
    ' the height test rather than by the range.
    while period <= n - 2
        r = _block_height(sh, n, 0, period, o.minimum_support)
        ' A BLOCK, not a line: two consecutive agreeing offsets at minimum.
        ' §7 Phase 3 in as many words -- "a line is not furniture merely
        ' because it repeats".
        if r.height >= 2 then
            if r.height > best_h then
                best_h = r.height
                best_p = period
                best_pages = r.pages
            end if
        end if
        period = period + 1
    end while

    if best_h >= 2 then
        starts = []
        ln = 0
        while ln < n
            append(starts, ln + 1)
            ln = ln + best_p
        end while
        return { starts: starts,
                 evidence: "a " + string(best_h) + "-line block repeating every "
                           + string(best_p) + " lines",
                 height: best_h }
    end if
    return { starts: [ 1 ], evidence: "none", height: 0 }
end function

' Lines whose SHAPE is stable at a fixed offset from the top of a page, across
' at least `minimum_support` of the pages.
'
' Returns { lines, pages, evidence, offsets, why } where `lines` holds the
' 1-based physical line numbers judged to be furniture -- the same thing the
' corpus's answer key records, so precision and recall are both computable
' against it rather than merely reported.
function furniture(rows, options = nothing)
    o = _options(options)
    ps = page_starts(rows, o)
    pages = count(ps.starts)

    ' A FORM FEED IS FURNITURE BY DEFINITION, not by inference. It is a page
    ' break character: nothing else it could be, and no support threshold
    ' applies to it. Missing this cost exactly one line per page break on every
    ' paginated source -- 8 of 9 found, which reads like a rounding error and is
    ' actually a whole category.
    out = []
    for each r in rows
        if r.form_feed then
            append(out, r.physical_line)
        end if
    end for

    offsets = []
    if pages < 2 then
        ' ONE PAGE IS NOT EVIDENCE OF FURNITURE, and this is a REFUSAL rather
        ' than an empty result. A single-page report's header is
        ' indistinguishable from its first heading -- the same lines, in the
        ' same place, appearing once -- so calling it furniture would strip a
        ' line section inference needs, and calling it content would be equally
        ' arbitrary. The honest answer names what is missing.
        return { lines: out, pages: pages, evidence: ps.evidence, offsets: offsets,
                 why: "only one page could be identified, so a header cannot be distinguished from a first heading; furniture needs at least two pages to compare" }
    end if

    ' FURNITURE IS THE CONTIGUOUS BLOCK FROM THE TOP OF THE PAGE, not any
    ' offset in a window that happens to agree.
    '
    ' MEASURED: with a fixed 8-line window, source 07 claimed 15 lines where 9
    ' were planted. The extras were offsets 4..7 where both pages happened to
    ' carry a detail row -- and `<IDENTIFIER> <TEXT> <TEXT> <DATE> <MONEY>` is
    ' the commonest shape in the document, so agreement there is nearly free.
    ' A header ENDS somewhere; the first offset that disagrees is where.
    '
    ' This is the same rule `_block_height` applies when inferring a period, so
    ' the form-feed path and the inferred path now agree about what a header
    ' block IS rather than only about where pages begin.
    sh = _shapes_of(rows)
    n = count(rows)
    first = ps.starts[0] - 1
    height = 0
    off = 0
    while off < 8
        if first + off >= n then
            break
        end if
        want = sh[first + off]
        seen = 0
        agree = 0
        for each st in ps.starts
            i = st - 1 + off
            if i < n then
                seen = seen + 1
                if sh[i] = want then
                    agree = agree + 1
                end if
            end if
        end for
        if seen < 2 then
            break
        end if
        if agree / seen < o.minimum_support then
            break
        end if
        if off = 0 then
            if want = "<BLANK>" then
                break
            end if
        end if
        ' THE LITERALS MUST AGREE TOO, not only the shapes.
        wants = _words_at(rows, n, ps.starts, off)
        wagree = 0
        for each w in wants
            if w = wants[0] then
                wagree = wagree + 1
            end if
        end for
        if wagree / count(wants) < o.minimum_support then
            break
        end if
        append(offsets, { offset: off, shape: want, words: wants[0],
                          seen: agree, pages: seen })
        for each st in ps.starts
            i = st - 1 + off
            if i < n then
                if sh[i] = want then
                    if not contains(out, st + off) then
                        append(out, st + off)
                    end if
                end if
            end if
        end for
        height = height + 1
        off = off + 1
    end while

    return { lines: out, pages: pages, evidence: ps.evidence,
             offsets: offsets, why: "" }
end function

' --- §7 Phase 4: families -------------------------------------------------
'
' §6.3: a signature "replaces variable spans with typed placeholders while
' preserving STABLE LITERALS". The word "stable" is doing all the work, and
' ONE LINE CANNOT DECIDE IT.
'
' The first version of this took every word as a literal, which is the obvious
' reading and is wrong in the direction that destroys the result: a member name
' is a word, so `00147454 REYES, YUKI 03/19/2026 947.08` yielded the signature
' `<IDENTIFIER> REYES YUKI <DATE> <MONEY>` and EVERY DETAIL ROW BECAME ITS OWN
' FAMILY -- 39 families in a report with four. Nothing errored; the profile was
' simply a list of every line.
'
' So stability is decided BY THE GROUP, in two passes:
'
'   1. group lines by SHAPE -- the kind sequence alone, no literals. Every
'      detail row shares one shape by construction.
'   2. within a group, at each word position, count the distinct words. One
'      word holding at least `minimum_support` of the group is a LITERAL there;
'      anything else is `<TEXT>`.
'
' That makes `ACCT` in a column heading a literal (it is in every member of its
' group) and `REYES` variable text (it is in a few members of a large one),
' from the same rule, with no list of "words that are probably labels".
'
' A SHAPE GROUP OF ONE IS LEFT ALONE rather than having every word blanked: a
' single occurrence is no evidence of variability either, and blanking it would
' throw away the literal a section heading is made of. It is reported with
' `support: 1` so a caller can see the distinction.

function _word_positions(rows, lines)
    ' For the lines in one shape group, the distinct words seen at each word
    ' position, as a map from position to a tally.
    tally = {}
    for each ln in lines
        i = 0
        for each sp in spans(rows[ln - 1].text)
            if sp.kind = "word" then
                k = string(i)
                w = upper(sp.text)
                if has(tally, k) then
                    t = tally[k]
                    if has(t, w) then
                        t[w] = t[w] + 1
                    else
                        t[w] = 1
                    end if
                    tally[k] = t
                else
                    tally[k] = { }
                    t2 = tally[k]
                    t2[w] = 1
                    tally[k] = t2
                end if
            end if
            i = i + 1
        end for
    end for
    return tally
end function

function _family_signature(rows, lines, options)
    o = _options(options)
    n = count(lines)
    wp = _word_positions(rows, lines)
    parts = []
    i = 0
    for each sp in spans(rows[lines[0] - 1].text)
        if sp.kind = "word" then
            k = string(i)
            dom = ""
            domn = 0
            if has(wp, k) then
                for each w in keys(wp[k])
                    if wp[k][w] > domn then
                        domn = wp[k][w]
                        dom = w
                    end if
                end for
            end if
            if n = 1 then
                append(parts, upper(sp.text))
            else
                if domn / n >= o.minimum_support then
                    append(parts, dom)
                else
                    append(parts, "<TEXT>")
                end if
            end if
        else
            append(parts, "<" + upper(sp.kind) + ">")
        end if
        i = i + 1
    end for
    if count(parts) = 0 then
        return shape(rows[lines[0] - 1].text)
    end if
    return join(parts, " ")
end function

' Lines grouped into families, with the minority groups RETAINED. "Retain
' minority clusters rather than forcing all lines into a dominant family" is
' §7's rule, and it is what keeps a totals row -- one line per section against
' dozens of detail rows, and shaped almost exactly like one -- from being
' absorbed into the family it sits under.
function families(rows, exclude_lines, options = nothing)
    o = _options(options)
    byshape = {}
    for each r in rows
        if not contains(exclude_lines, r.physical_line) then
            if not r.blank then
                ' INDENT IS PART OF THE GROUP KEY, and §6.3 already asks for it
                ' ("preserve useful features such as indentation"). Without it
                ' a column heading `ACCT MEMBER NAME POSTED AMOUNT` and a
                ' remark note `reconciled against the general ledger` have the
                ' SAME shape -- five words -- so they merge, no word is
                ' dominant in the merged group, and both come back as
                ' `<TEXT> <TEXT> <TEXT> <TEXT> <TEXT>`: two families lost and
                ' the anchor `ACCT` destroyed. Indent is stable within a source
                ' and is exactly what separates them.
                sh = string(r.indent) + "|" + shape(r.text)
                if has(byshape, sh) then
                    p = byshape[sh]
                    append(p, r.physical_line)
                    byshape[sh] = p
                else
                    byshape[sh] = [ r.physical_line ]
                end if
            end if
        end if
    end for

    out = []
    for each sh in keys(byshape)
        lines = byshape[sh]
        sg = _family_signature(rows, lines, o)
        ex = []
        for each ln in lines
            if count(ex) < 3 then
                append(ex, ln)
            end if
        end for
        append(out, { signature: sg, shape: sh, count: count(lines),
                      examples: ex, lines: lines })
    end for
    return out
end function

' --- §9 the profile -------------------------------------------------------

function profile(report_text, options = nothing)
    return profile_source({ id: "source", text: report_text }, options)
end function

function profile_source(source, options = nothing)
    o = _options(options)
    if not has(source, "text") then
        error "ari_discover.profile_source: a source needs `text`"
    end if
    sid = "source"
    if has(source, "id") then
        sid = source.id
    end if
    g = grid(sid, source.text)
    f = furniture(g, o)
    fam = families(g, f.lines, o)

    blanks = 0
    content = 0
    for each r in g
        if r.blank then
            blanks = blanks + 1
        else
            if not contains(f.lines, r.physical_line) then
                content = content + 1
            end if
        end if
    end for

    return { id: sid,
             physical_lines: count(g),
             bytes: byte_count(source.text),
             blank_lines: blanks,
             content_lines: content,
             furniture: f,
             families: fam,
             grid: g }
end function

' Across a corpus. §5.1: variation between files is what separates a true
' constant from an accidental one, so a signature seen in ONE source is
' reported with the count of sources that carry it and not merely its
' frequency -- which is the number `minimum_support` is about.
function profile_corpus(sources, options = nothing)
    o = _options(options)
    if type(sources) != "array" then
        error "ari_discover.profile_corpus expects an array of sources"
    end if
    if count(sources) = 0 then
        error "ari_discover.profile_corpus: no sources"
    end if
    profiles = []
    across = {}
    for each s in sources
        p = profile_source(s, o)
        append(profiles, p)
        seen = []
        for each fm in p.families
            if not contains(seen, fm.signature) then
                append(seen, fm.signature)
            end if
        end for
        for each sg in seen
            if has(across, sg) then
                across[sg] = across[sg] + 1
            else
                across[sg] = 1
            end if
        end for
    end for

    nsrc = count(profiles)
    shared = []
    only_one = []
    for each sg in keys(across)
        entry = { signature: sg, sources: across[sg], support: across[sg] / nsrc }
        if entry.support >= o.minimum_support then
            append(shared, entry)
        end if
        if across[sg] = 1 then
            append(only_one, entry)
        end if
    end for

    ' §5.2: the threshold cannot be calibrated on a corpus this small, and
    ' saying so beats reporting a support figure that can only take a few
    ' values. Ten is where a tenth of a point of support is one source.
    warn = ""
    if nsrc < 10 then
        warn = ("support over " + string(nsrc) + " sources can only take "
                + string(nsrc) + " distinct values, so `minimum_support` cannot "
                + "be calibrated here; at least 10 sources are needed for the "
                + "threshold to mean what it says")
    end if

    return { sources: nsrc,
             profiles: profiles,
             shared_signatures: shared,
             single_source_signatures: only_one,
             support_warning: warn }
end function

' --- §12 a human-readable profile ----------------------------------------

function explain(p, options = nothing)
    o = _options(options)
    out = []
    append(out, "source: " + string(p.id))
    append(out, "  physical lines   " + string(p.physical_lines)
                + "   (" + string(p.bytes) + " bytes)")
    append(out, "  blank            " + string(p.blank_lines))
    append(out, "  content          " + string(p.content_lines))
    append(out, "  furniture        " + string(count(p.furniture.lines))
                + "   pages " + string(p.furniture.pages)
                + " (" + p.furniture.evidence + ")")
    if p.furniture.why != "" then
        append(out, "                   " + p.furniture.why)
    end if
    append(out, "  row families     " + string(count(p.families)))
    ranked = _by_count_desc(p.families)
    shown = 0
    for each fm in ranked
        if shown < 8 then
            append(out, "    " + _pad(string(fm.count), 5) + "  " + fm.signature)
            shown = shown + 1
        end if
    end for
    if count(ranked) > 8 then
        append(out, "    ...   " + string(count(ranked) - 8) + " more")
    end if
    return join(out, "\n")
end function

function _pad(s, w)
    n = w - len(s)
    if n <= 0 then
        return s
    end if
    return repeat(" ", n) + s
end function

function _by_count_desc(items)
    out = []
    taken = []
    n = count(items)
    j = 0
    while j < n
        best = -1
        k = 0
        while k < n
            if not contains(taken, k) then
                if best < 0 then
                    best = k
                else
                    if items[k].count > items[best].count then
                        best = k
                    end if
                end if
            end if
            k = k + 1
        end while
        append(taken, best)
        append(out, items[best])
        j = j + 1
    end while
    return out
end function

end library
