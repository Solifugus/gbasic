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
             minimum_family_share: 0.15,
             minimum_variant_sources: 3,
             holdout: 0,
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
' `profiles_in` may be supplied when the caller has already profiled these
' sources. Profiling is the expensive half -- `furniture` searches for a period
' and `shape` is regex-heavy -- and `variants` profiles a corpus once and then
' asks about it several ways, so re-profiling would be the same answer at
' several times the cost. It is the treatment `anchor_stability` and
' `region_coverage` already take.
function profile_corpus(sources, options = nothing, profiles_in = nothing)
    o = _options(options)
    if type(sources) != "array" then
        error "ari_discover.profile_corpus expects an array of sources"
    end if
    if count(sources) = 0 then
        error "ari_discover.profile_corpus: no sources"
    end if
    profiles = []
    across = {}
    pi = 0
    for each s in sources
        if is_nothing(profiles_in) then
            p = profile_source(s, o)
        else
            p = profiles_in[pi]
        end if
        pi = pi + 1
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

' --- §16 PHASE 1: infer a specification, and let `ari` judge it -------------
'
' Design principle 4: THE RUNTIME IS THE JUDGE. A proposed rule is not
' successful until ordinary `ari.parse` executes it against the corpus. Nothing
' below scores a candidate from the inference model that produced it -- the
' model's opinion of its own work is the one number that cannot be evidence.
'
' WHAT PHASE 1 CAN AND CANNOT LOCATE, and the limit is the REPORT's, not a
' shortcoming here. A detail row carries no literal anchors:
'
'     00147454    REYES, YUKI              03/19/2026         947.08
'
' so `right of "..."` and `between ... and ...` have nothing to attach to. What
' remains anchor-relative is `first`/`last <type>`, which reaches the money and
' the date and nothing else. The identifier and the name are reachable ONLY by
' column.
'
' THAT COST IS REPORTED, NEVER PAID SILENTLY. §5.2 defaults `allow_fixed_columns`
' to false because a column rule breaks the moment the report drifts, which is
' the whole reason ARI is anchor-relative. So by default those fields become
' QUESTIONS carrying their evidence (§10), and turning columns on answers them
' at a price the scorecard states as `positional_dependence`.

function _kind_locator(kind)
    ' Which kinds `first`/`last` can reach at all. `identifier` is deliberately
    ' absent: `first integer` on `00147454` answers 147454 -- LEADING ZEROS
    ' GONE, and a number where the source had a code. An account number that
    ' reads back shorter is exactly the ordinary-looking wrong value this
    ' library exists to refuse.
    if kind = "money" then
        return "money"
    end if
    if kind = "date" then
        return "date"
    end if
    return ""
end function

' Name a field from the column heading above it, when the heading's word
' overlaps the field's own column range. §3: "likely field names derived from
' headings or adjacent labels".
'
' OVERLAP, NOT ORDER. Matching the nth heading word to the nth field assumes
' both have the same count, and a two-word heading over a one-value column
' ("MEMBER NAME") breaks it silently, naming everything after it wrongly.
function _name_from_heading(heading_text, cp_start, cp_length)
    if heading_text = "" then
        return ""
    end if
    parts = []
    lo = cp_start
    hi = cp_start + cp_length
    for each sp in spans(heading_text)
        a = sp.cp_start
        b = sp.cp_start + sp.cp_length
        if a < hi then
            if lo < b then
                append(parts, lower(sp.text))
            end if
        end if
    end for
    if count(parts) = 0 then
        return ""
    end if
    return join(parts, "_")
end function

function _safe_name(n, used, idx)
    base = n
    if base = "" then
        base = "field_" + string(idx + 1)
    end if
    base = replace(base, " ", "_")
    if contains(used, base) then
        return base + "_" + string(idx + 1)
    end if
    return base
end function

' The heading line for a family: the nearest line ABOVE its first member that is
' itself a family of words and is not part of this family.
function _heading_for(rows, fam, furniture_lines)
    first = fam.lines[0]
    ln = first - 1
    seen = 0
    while ln >= 1
        if seen > 3 then
            return ""
        end if
        r = rows[ln - 1]
        if not contains(furniture_lines, ln) then
            if not r.blank then
                if shape(r.text) != fam.shape then
                    if _is_rule(trim(r.text)) then
                        ln = ln - 1
                        seen = seen + 1
                        continue
                    end if
                    ' A heading is all words. A line carrying money or a date is
                    ' a data row, not a caption for one.
                    allwords = true
                    for each sp in spans(r.text)
                        if sp.kind != "word" then
                            allwords = false
                        end if
                    end for
                    if allwords then
                        return r.text
                    end if
                    return ""
                end if
            end if
        end if
        ln = ln - 1
        seen = seen + 1
    end while
    return ""
end function

' The fields of one family, with a locator each and the evidence for it.
' THE COLUMNS OF A FIXED-WIDTH FAMILY ARE ITS GUTTERS, not the extent of the
' values that happened to be in it.
'
' The first version took each span's min..max across the family. MEASURED, that
' is wrong twice over and both failures are silent:
'
'   * `columns 4-11` on an account column starting at 2 returned `147454` --
'     THE LEADING ZEROS GONE, a number where the source had a code;
'   * a member name came back as `YES, YUKI`, because the widest observed
'     surname still did not reach the column's real right edge.
'
' A column is bounded by whitespace that is present on EVERY row of the family,
' which is what a person reads by eye and what the report generator actually
' emitted. A run of one space is not a gutter -- `REYES, YUKI` has one inside a
' single value, and the position of that space MOVES with the surname's length,
' which is precisely why the all-rows test settles it.
'
' This also merges spans correctly without a rule about merging: a surname and a
' forename separated by one space fall in ONE column and become ONE field, while
' an account and a name separated by a four-space gutter stay two.
function gutters(rows, lines)
    width = 0
    for each ln in lines
        w = len(rows[ln - 1].text)
        if w > width then
            width = w
        end if
    end for
    ' A position is a gutter if EVERY row of the family has a space there (or
    ' ends before it).
    isgap = []
    i = 0
    while i < width
        allspace = true
        for each ln in lines
            t = rows[ln - 1].text
            if i < len(t) then
                if mid(t, i, 1) != " " then
                    allspace = false
                end if
            end if
        end for
        append(isgap, allspace)
        i = i + 1
    end while

    ' Columns are the runs BETWEEN gutters of two or more positions. A
    ' single-space gap is inside a value, not between columns.
    cols = []
    i = 0
    start = -1
    while i <= width
        gap = true
        if i < width then
            gap = isgap[i]
        end if
        runlen = 0
        if gap then
            j = i
            while j < width
                if not isgap[j] then
                    break
                end if
                runlen = runlen + 1
                j = j + 1
            end while
        end if
        if gap and runlen >= 2 then
            if start >= 0 then
                append(cols, { cp_start: start, cp_end: i - 1 })
                start = -1
            end if
            i = i + runlen
        else
            if i < width then
                if start < 0 then
                    start = i
                end if
            end if
            i = i + 1
        end if
    end while
    if start >= 0 then
        append(cols, { cp_start: start, cp_end: width - 1 })
    end if
    return cols
end function

function _column_of(cols, cp)
    i = 0
    while i < count(cols)
        if cp >= cols[i].cp_start then
            if cp <= cols[i].cp_end then
                return i
            end if
        end if
        i = i + 1
    end while
    return -1
end function

' The fields of one family: ONE COLUMN IS ONE FIELD, with a locator each and
' the evidence for it.
function infer_fields(rows, fam, furniture_lines, options = nothing)
    o = _options(options)
    heading = _heading_for(rows, fam, furniture_lines)
    cols = gutters(rows, fam.lines)

    ' What kind lives in each column, decided across the WHOLE family rather
    ' than from one row: a column is typed only if every row agrees, because a
    ' column that is money on most rows and text on one is not a money column,
    ' it is a column with a problem in it.
    kinds = []
    consistent = []
    ci = 0
    while ci < count(cols)
        tally = {}
        rowsseen = 0
        for each ln in fam.lines
            rowsseen = rowsseen + 1
            here = []
            for each sp in spans(rows[ln - 1].text)
                if _column_of(cols, sp.cp_start) = ci then
                    append(here, sp.kind)
                end if
            end for
            k = "text"
            if count(here) = 1 then
                k = here[0]
            end if
            if has(tally, k) then
                tally[k] = tally[k] + 1
            else
                tally[k] = 1
            end if
        end for
        top = 0
        topk = "text"
        for each k in keys(tally)
            if tally[k] > top then
                top = tally[k]
                topk = k
            end if
        end for
        append(kinds, topk)
        append(consistent, top / rowsseen)
        ci = ci + 1
    end while

    ' How many columns carry each typed kind decides whether `first`/`last` is
    ' unambiguous.
    counts = {}
    for each k in kinds
        if has(counts, k) then
            counts[k] = counts[k] + 1
        else
            counts[k] = 1
        end if
    end for

    fields = []
    questions = []
    used = []
    seen_kind = {}
    ci = 0
    while ci < count(cols)
        c = cols[ci]
        k = kinds[ci]
        if has(seen_kind, k) then
            seen_kind[k] = seen_kind[k] + 1
        else
            seen_kind[k] = 1
        end if
        nth = seen_kind[k]

        nm = _safe_name(_name_from_heading(heading, c.cp_start, c.cp_end - c.cp_start + 1),
                        used, ci)
        append(used, nm)
        sample = trim(mid(rows[fam.lines[0] - 1].text, c.cp_start, c.cp_end - c.cp_start + 1))

        ty = _kind_locator(k)
        loc = ""
        why = ""
        positional = false
        if ty != "" then
            if counts[k] = 1 then
                loc = "first " + ty
                why = "the only " + ty + " column on the row"
            else
                if nth = 1 then
                    loc = "first " + ty
                    why = "the first of " + string(counts[k]) + " " + ty + " columns"
                else
                    if nth = counts[k] then
                        loc = "last " + ty
                        why = "the last of " + string(counts[k]) + " " + ty + " columns"
                    end if
                end if
            end if
        end if

        if loc = "" then
            if o.allow_fixed_columns then
                loc = "columns " + string(c.cp_start) + "-" + string(c.cp_end)
                why = ("no anchor-relative locator reaches a " + k + " column, so"
                       + " this is POSITIONAL and breaks if the report drifts")
                positional = true
            else
                append(questions, { field: nm, kind: k,
                                    cp_start: c.cp_start,
                                    cp_length: c.cp_end - c.cp_start + 1,
                                    example: sample,
                                    why: ("a " + k + " column at " + string(c.cp_start)
                                          + "-" + string(c.cp_end)
                                          + " that no anchor-relative rule can reach:"
                                          + " the row carries no literal to anchor to,"
                                          + " and `first`/`last` reaches only money and date."),
                                    options: [ "enable allow_fixed_columns and accept a positional rule",
                                               "supply an anchor this adapter cannot see",
                                               "leave the field out" ] })
            end if
        end if

        if loc != "" then
            as_type = ""
            if k = "money" then
                as_type = " as money"
            end if
            if k = "date" then
                as_type = " as date"
            end if
            append(fields, { name: nm, locator: loc, type: k,
                             as_type: as_type, positional: positional,
                             cp_start: c.cp_start, cp_length: c.cp_end - c.cp_start + 1,
                             example: sample, consistency: consistent[ci],
                             why: why })
        end if
        ci = ci + 1
    end while
    return { fields: fields, questions: questions, heading: heading,
             columns: cols }
end function

' The generated specification. Comments carry the evidence (§7 Phase 7), because
' a person has to maintain this afterwards WITHOUT ari_discover -- design
' principle 8.
function spec_text(fam, inferred, options = nothing)
    return spec_text_nested(fam, inferred, unknown, unknown, options)
end function

' The generated specification. Comments carry the evidence (§7 Phase 7),
' because a person has to maintain this afterwards WITHOUT ari_discover --
' design principle 8.
function spec_text_nested(fam, inferred, sect, page, options = nothing)
    o = _options(options)
    out = []
    append(out, "' Generated by ari_discover.")
    append(out, "' Detail family: " + fam.signature)
    append(out, "'   " + string(fam.count) + " lines, examples at "
                + join(_strs(fam.examples), ", "))
    if inferred.heading != "" then
        append(out, "' Field names taken from the column heading above it.")
    else
        append(out, "' NO column heading was found: field names are positional.")
    end if

    if not is_unknown(page) then
        for each l in page
            append(out, l)
        end for
    end if

    append(out, "section report:")
    ind = "    "
    if is_unknown(sect) then
        append(out, ind + "section rows repeats starts(/" + _start_regex(fam) + "/):")
        ind = ind + "    "
    else
        append(out, ind + "' one section per run of detail rows, headed by "
                    + "\"" + sect.heading.literal + "\"")
        append(out, ind + "section groups repeats starts(/^" + sect.heading_pattern + " /):")
        ind = ind + "    "
        for each hf in sect.heading_fields
            append(out, ind + "' " + hf.why)
            append(out, ind + "field " + hf.name + ": " + hf.locator + hf.as_type)
        end for
        if not is_unknown(sect.total) then
          if sect.total_token != "" then
            append(out, ind + "' the closing amount of the run, by its own label")
            append(out, ind + "field group_total: right of " + sect.total_token + " as money")
          end if
        end if
        append(out, ind + "section rows repeats starts(/" + _start_regex(fam) + "/):")
        ind = ind + "    "
    end if
    for each f in inferred.fields
        append(out, ind + "' " + f.why)
        append(out, ind + "field " + f.name + ": " + f.locator + f.as_type)
    end for
    return join(out, "\n")
end function

function _strs(a)
    out = []
    for each x in a
        append(out, string(x))
    end for
    return out
end function

' A `starts(...)` pattern for the family, built from what its first span IS
' rather than from the literal text of one row.
function _start_regex(fam)
    parts = [ "^" ]
    ' Leading indent is part of the family key, so it is evidence.
    return "^[ ]*" + _first_span_regex(fam)
end function

function _first_span_regex(fam)
    sg = fam.signature
    if starts_with(sg, "<IDENTIFIER>") then
        return "[0-9]{5,}"
    end if
    if starts_with(sg, "<MONEY>") then
        return "[0-9,]+\\.[0-9]{2}"
    end if
    if starts_with(sg, "<DATE>") then
        return "[0-9]"
    end if
    if starts_with(sg, "<NUMBER>") then
        return "[0-9]"
    end if
    if starts_with(sg, "<TEXT>") then
        return "[A-Za-z]"
    end if
    ' A literal first token is the strongest start pattern available.
    w = sg
    sp = find(sg, " ")
    if not is_nothing(sp) then
        w = mid(sg, 0, sp)
    end if
    return w
end function

' --- §8 scoring: the runtime is the judge ----------------------------------
'
' Every number here comes from RUNNING the candidate, never from the model that
' produced it.
'
' TWO OF §8'S NINE MEASURES WERE REPORTED `unknown` UNTIL 2026-09-18, and the
' reason was a capability gap rather than a judgement: `content_coverage` and
' `collision_rate` both need to know WHICH SOURCE SPANS A RULE CLAIMED, and
' `ari` had no span-level surface -- entry C1 in docs/ari_limitations.md, struck
' when `ari.trace` shipped. They come from RUNNING the candidate over the
' corpus, like every other number here; estimating them from the inference model
' would have been the tool grading its own homework, which is the one thing
' design principle 4 forbids, and is exactly what an estimate would have been.
'
' COVERAGE IS POOLED OVER THE CORPUS, not averaged over the sources: a per-source
' mean lets a short source that happens to be fully explained offset a long one
' that is not, and what is being asked is how much of the ESTATE OF TEXT the
' specification accounts for.
' §8 REGION COVERAGE, and it is the measure that catches what
' `source_coverage` cannot.
'
' MEASURED: a nested specification built from one source parsed all eight and
' reported `source_coverage 1.0` while finding the WRONG NUMBER OF SECTIONS in
' every one of them -- 4 where the source has 3, 10 where it has 7. Three
' separate things in the generated spec are source-specific and none of them
' fails loudly:
'
'   * `break: formfeed` does NOTHING on a source paginated by a header line, so
'     the page header `BRANCH ACTIVITY REGISTER` matches `^BRANCH ` and becomes
'     a section;
'   * the total's label differs (`BRANCH TOTAL` / `TOTAL FOR BRANCH`), so
'     `right of "..."` finds nothing;
'   * the columns differ, which Phase 1 already measured.
'
' So region coverage compares what the SPEC found against what PHASE 0
' MEASURED -- the number of runs of the detail family -- which needs no answer
' key and is exactly §8's "fraction of inferred regions claimed by the
' specification".
function region_coverage(sources, spec, detail_signature, options = nothing, profiles = nothing)
    o = _options(options)
    agree = 0
    per = []
    si = 0
    for each s in sources
        if is_nothing(profiles) then
            g = grid(s.id, s.text)
            f = furniture(g, o)
            fams = families(g, f.lines, o)
            fl = f.lines
        else
            g = profiles[si].grid
            fams = profiles[si].families
            fl = profiles[si].furniture.lines
        end if
        si = si + 1
        want = 0
        for each fm in fams
            if fm.signature = detail_signature then
                want = count(_runs_of(fm.lines, fl, g))
            end if
        end for
        got = 0
        on error goto next
        r = ari.parse(s.text, spec)
        if error then
            error.clear()
            on error stop
            append(per, { id: s.id, wanted: want, found: 0, agrees: false })
            continue
        end if
        on error stop
        if r.ok then
            if has(r.value, "groups") then
                got = count(r.value.groups)
            else
                if has(r.value, "rows") then
                    got = 1
                    want = 1
                end if
            end if
        end if
        ok = got = want
        if ok then
            agree = agree + 1
        end if
        append(per, { id: s.id, wanted: want, found: got, agrees: ok })
    end for
    return { coverage: agree / count(sources), sources: count(sources),
             agreeing: agree, per_source: per }
end function

' Rows and cells in a parse result, FLAT OR NESTED.
'
' It used to look only at a top-level `rows`, so once Phase 2 started
' generating a nested specification every proposal reported `rows: 0` and
' `cells: 0` -- and therefore `unknown_rate: 0`, which reads as "nothing is
' unknown" when what happened is that nothing was counted. An absence of
' measurement rendered as a clean 0% is exactly the shape of defect this
' library exists to report rather than produce.
function _count_rows(v)
    rows = 0
    cells = 0
    unk = 0
    if type(v) != "record" then
        return { rows: 0, cells: 0, unknown: 0 }
    end if
    if has(v, "rows") then
        for each row in v.rows
            rows = rows + 1
            for each k in keys(row)
                cells = cells + 1
                if is_unknown(row[k]) then
                    unk = unk + 1
                end if
            end for
        end for
    end if
    if has(v, "groups") then
        for each grp in v.groups
            ' A group carries its own fields (the section heading's number, the
            ' total) as well as its rows. Those are cells too: a section total
            ' that came back unknown is the failure `corpus_labels` exists to
            ' prevent, and it must show up in the rate.
            for each k in keys(grp)
                if k != "rows" then
                    if k != "groups" then
                        cells = cells + 1
                        if is_unknown(grp[k]) then
                            unk = unk + 1
                        end if
                    end if
                end if
            end for
            inner = _count_rows(grp)
            rows = rows + inner.rows
            cells = cells + inner.cells
            unk = unk + inner.unknown
        end for
    end if
    return { rows: rows, cells: cells, unknown: unk }
end function

function validate(sources, spec, options = nothing)
    o = _options(options)
    if type(sources) != "array" then
        error "ari_discover.validate expects an array of sources"
    end if
    parsed_ok = 0
    rows_total = 0
    cells_total = 0
    cells_unknown = 0
    content_chars = 0
    claimed_chars = 0
    claims_total = 0
    value_claims = 0
    failures = []
    for each s in sources
        on error goto next
        r = ari.parse(s.text, spec)
        if error then
            append(failures, { id: s.id, why: error.message })
            error.clear()
            on error stop
            continue
        end if
        on error stop
        if not r.ok then
            append(failures, { id: s.id, why: r.message })
            continue
        end if
        parsed_ok = parsed_ok + 1
        tal = _count_rows(r.value)
        rows_total = rows_total + tal.rows
        cells_total = cells_total + tal.cells
        cells_unknown = cells_unknown + tal.unknown

        tr = ari.trace(s.text, spec)
        if tr.ok then
            content_chars = content_chars + tr.content_chars
            claimed_chars = claimed_chars + tr.claimed_chars
            value_claims = value_claims + tr.collisions_involved
            for each c in tr.claims
                if c.kind = "field" then
                    claims_total = claims_total + 1
                end if
            end for
        end if
    end for
    ur = 0
    if cells_total > 0 then
        ur = cells_unknown / cells_total
    end if
    ' `unknown`, never 0. Nothing parsed means nothing was measured, and a 0
    ' there reads as a specification that explained none of the text -- a
    ' different claim, and one a reader would act on.
    cc = unknown
    if content_chars > 0 then
        cc = claimed_chars / content_chars
    end if
    cr = unknown
    if claims_total > 0 then
        cr = value_claims / claims_total
    end if
    return { source_coverage: parsed_ok / count(sources),
             sources: count(sources),
             parsed: parsed_ok,
             rows: rows_total,
             cells: cells_total,
             unknown_rate: ur,
             failures: failures,
             content_chars: content_chars,
             claimed_chars: claimed_chars,
             content_coverage: cc,
             content_coverage_is: ("non-blank characters on non-furniture lines"
                 + " claimed by a field rule or a section heading, pooled over"
                 + " every source that parsed, over all non-blank characters on"
                 + " those lines"),
             claims: claims_total,
             collisions_involved: value_claims,
             collision_rate: cr,
             collision_rate_is: ("value claims whose character extent overlaps"
                 + " another rule's claim, over all value claims, pooled over"
                 + " every source that parsed"),
             not_computable: [ ],
             not_computable_why: "" }
end function

' --- §16 PHASE 2: sections, and the furniture directive ---------------------
'
' A flat `rows` specification loses the thing a report is FOR: which branch a
' row belongs to. Phase 2 nests it, and two things have to be inferred that
' Phase 1 never needed.
'
' THE FURNITURE DIRECTIVE COMES FIRST, and without it the rest is wrong rather
' than merely incomplete. MEASURED: the page header `BRANCH ACTIVITY REGISTER`
' matches `^BRANCH ` exactly as a branch heading does, so a nested spec without
' `page:` found FIVE sections in a three-branch report -- two of them page
' headers carrying a branch number read out of `PAGE 1`, and two real branches
' split across a page boundary reporting `total: unknown` and `rows: 0`. Phase 0
' already measured the furniture; Phase 2 is where that measurement becomes a
' `break:`/`drop:` pair the engine acts on.
'
' INDENTATION DISAMBIGUATES, and it is load-bearing. `BRANCH 46 HARBOUR` sits at
' column 0 and `  BRANCH TOTAL  16,405.29` is indented, so `^BRANCH ` starts a
' section and does NOT match the total inside it. Phase 0's family key already
' carries indent, which is why that is available rather than lucky.

function _literal_prefix(sg)
    ' The leading LITERAL words of a family signature -- what a `starts(...)`
    ' pattern can be anchored on. A signature beginning with a placeholder has
    ' none, and such a family cannot start a section.
    out = []
    for each tok in split(sg, " ")
        if starts_with(tok, "<") then
            break
        end if
        append(out, tok)
    end for
    return join(out, " ")
end function

' Maximal blocks of a family's lines. A section contains one run, so the number
' of runs is how many sections there should be.
'
' A GAP OF FURNITURE DOES NOT BREAK A RUN, and getting that wrong made the
' measure disagree with the engine it is measuring. A branch whose detail rows
' straddle a page boundary has a page header in the middle of them, so by raw
' line numbers it is TWO runs -- but `page:` strips that header before any
' section is located, so `ari` sees one. MEASURED: region coverage reported
' `wanted 8, found 7` on the two sources with the most branches, and the SEVEN
' was right: the specification had found every section and the proxy had
' counted one too many.
'
' `skippable` is the furniture; a blank line between rows of one table is
' likewise not a section boundary.
function _runs_of(lines, skippable = nothing, rows = nothing)
    runs = []
    i = 0
    while i < count(lines)
        lo = lines[i]
        hi = lines[i]
        while i + 1 < count(lines)
            nxt = lines[i + 1]
            joined = nxt = hi + 1
            if not joined then
                if not is_nothing(skippable) then
                    ' Everything between must be furniture or blank.
                    ok = true
                    j = hi + 1
                    while j < nxt
                        if not contains(skippable, j) then
                            if is_nothing(rows) then
                                ok = false
                            else
                                if not rows[j - 1].blank then
                                    ok = false
                                end if
                            end if
                        end if
                        j = j + 1
                    end while
                    joined = ok
                end if
            end if
            if joined then
                hi = nxt
                i = i + 1
            else
                break
            end if
        end while
        append(runs, { lo: lo, hi: hi })
        i = i + 1
    end while
    return runs
end function

' Which family heads each run of detail rows, and which closes it.
'
' Decided by POSITION relative to the runs rather than by what the text says: a
' heading is whatever consistently appears above a run, a total is whatever
' consistently appears below one. Keying on words like "TOTAL" would work on
' this corpus and on no report that called it "SUMMARY".
function sections(rows, fams, detail, furniture_lines, options = nothing)
    o = _options(options)
    runs = _runs_of(detail.lines, furniture_lines, rows)

    ' CANDIDATES ARE GROUPED BY LITERAL PREFIX, NOT BY FAMILY, because that is
    ' what a `starts(...)` pattern is built from.
    '
    ' MEASURED: keyed by family, no heading was found at all. `BRANCH 46
    ' HARBOUR` and `BRANCH 25 OLD MILL` are DIFFERENT families -- a two-word
    ' branch name gives a different shape -- so the eight headings in a source
    ' split across three families and none of them cleared the support
    ' threshold on its own. They share the prefix the pattern actually uses.
    groups = {}
    for each fm in fams
        if fm.signature = detail.signature then
            continue
        end if
        lit = _literal_prefix(fm.signature)
        if lit = "" then
            continue
        end if
        e = { literal: lit, lines: [], varies: false, has_money: false,
              indent: rows[fm.lines[0] - 1].indent }
        if has(groups, lit) then
            e = groups[lit]
        end if
        for each ln in fm.lines
            append(e.lines, ln)
        end for
        if contains(fm.signature, "<") then
            e.varies = true
        end if
        if contains(fm.signature, "<MONEY>") then
            e.has_money = true
        end if
        if rows[fm.lines[0] - 1].indent < e.indent then
            e.indent = rows[fm.lines[0] - 1].indent
        end if
        groups[lit] = e
    end for

    heading = unknown
    total = unknown
    for each lit in keys(groups)
        e = groups[lit]
        above = 0
        below = 0
        for each r in runs
            for each ln in e.lines
                if ln < r.lo then
                    if r.lo - ln <= 4 then
                        above = above + 1
                    end if
                end if
                if ln > r.hi then
                    if ln - r.hi <= 4 then
                        below = below + 1
                    end if
                end if
            end for
        end for
        if above >= count(runs) * o.minimum_support then
            if not e.has_money then
                ' A SECTION HEADING MUST VARY. The column heading
                ' `ACCT MEMBER NAME POSTED AMOUNT` sits above every run too and
                ' is CLOSER to it, so nearness alone chose it -- and the
                ' generated `starts(...)` then matched nothing at all, because
                ' that line is indented while the pattern was anchored at
                ' column 0. The discriminator is neither distance nor indent: a
                ' heading IDENTICAL EVERY TIME cannot say which section you are
                ' in. `BRANCH 46 HARBOUR` carries a number and a name; a column
                ' caption carries nothing that varies.
                ' HONEST NOTE, MEASURED 2026-09-17: neither this rule nor the
                ' outermost-indent preference below is load-bearing ON THIS
                ' CORPUS. Removing both changes no answer, because the branch
                ' heading appears before the column caption in every source and
                ' insertion order then picks it. So the right heading is
                ' currently chosen BY ACCIDENT here, and these two rules are
                ' the reason it would still be chosen if the order changed.
                '
                ' Both have a reason that this corpus cannot exhibit. `varies`
                ' is semantic: a heading identical every time cannot say WHICH
                ' section you are in. The indent preference is for NESTED
                ' sections -- region above branch above rows, which
                ' examples/fixtures/ari/delinquency.rpt has and this corpus does
                ' not. Recorded rather than proven, and recorded rather than
                ' removed, because a rule with a reason is not dead code.
                if e.varies then
                    take = is_unknown(heading)
                    if not take then
                        ' Prefer the OUTERMOST: a section encloses its content,
                        ' so where two qualify the less-indented one contains
                        ' the other.
                        if e.indent < heading.indent then
                            take = true
                        end if
                    end if
                    if take then
                        heading = { literal: lit, seen: above, indent: e.indent,
                                    lines: e.lines }
                    end if
                end if
            end if
        end if
        if below >= count(runs) * o.minimum_support then
            if e.has_money then
                if is_unknown(total) then
                    total = { literal: lit, seen: below, lines: e.lines }
                end if
            end if
        end if
    end for
    return { runs: count(runs), heading: heading, total: total }
end function

' §16 Phase 2, multi-source refinement: the section and total labels ACROSS the
' corpus rather than from the one source the rest was built from.
'
' MEASURED: with the label taken from one source, the generated
' `right of "TOTAL FOR BRANCH"` recovered 19 of 37 branch totals -- exactly the
' three sources that happen to use that wording. The other five say
' `BRANCH TOTAL`, and the field came back `unknown` with nothing to say why.
'
' `ari` takes a REGEX token in a locator, so the remedy is one specification
' carrying the alternation rather than two specifications or a lost field. That
' is §13's "one specification with alternate sections", reached from evidence
' instead of from a guess about which wording is canonical.
function corpus_labels(profiles, detail_signature, options = nothing)
    o = _options(options)
    headings = []
    totals = []
    per = 0
    for each p in profiles
        det = unknown
        for each fm in p.families
            if fm.signature = detail_signature then
                det = fm
            end if
        end for
        if is_unknown(det) then
            continue
        end if
        per = per + 1
        sc = sections(p.grid, p.families, det, p.furniture.lines, o)
        if not is_unknown(sc.heading) then
            if not contains(headings, sc.heading.literal) then
                append(headings, sc.heading.literal)
            end if
        end if
        if not is_unknown(sc.total) then
            if not contains(totals, sc.total.literal) then
                append(totals, sc.total.literal)
            end if
        end if
    end for
    return { headings: headings, totals: totals, sources: per }
end function

' A locator token for a set of observed literals: a quoted string when the
' corpus agrees, a regex alternation when it does not.
'
' Joined with `[ ]+` for the same reason the page break is: a print-image label
' is column-aligned and the literal recorded in a signature has its gaps
' normalised away.
function label_token(literals)
    if count(literals) = 0 then
        return ""
    end if
    if count(literals) = 1 then
        return "\"" + literals[0] + "\""
    end if
    alts = []
    for each lit in literals
        parts = []
        okall = true
        for each w in split(lit, " ")
            if is_unknown(match(w, regex("^[A-Za-z0-9]+$"))) then
                okall = false
            end if
            append(parts, w)
        end for
        if okall then
            append(alts, join(parts, "[ ]+"))
        end if
    end for
    if count(alts) = 0 then
        return ""
    end if
    return "/" + join(alts, "|") + "/"
end function

' The `page:` block, from what Phase 0 measured rather than from a guess --
' and from what the WHOLE CORPUS shares rather than what one source happens to
' be.
'
' MEASURED, and the difference is the entire result of multi-source refinement:
'
'     break: formfeed                      0 / 8 sources with the right
'                                              section count
'     break: /^BRANCH ACTIVITY REGISTER/   8 / 8
'
' Both describe the source the specification was built from. Only one describes
' the others. The form feed is present in half the corpus; THE HEADER LINE IS
' PRESENT IN ALL OF IT, including the sources that also carry a form feed -- so
' the regex is evidence the corpus shares and `formfeed` is evidence one source
' happens to have.
'
' THE RULE IS NOT "PREFER REGEX". It is prefer the directive every source
' supports, and when they all carry form feeds `formfeed` is the better answer:
' it cannot be defeated by a header whose wording drifts.
function furniture_directive(f)
    return _page_block(f, true)
end function

' A FORM FEED IS A FALLBACK, NEVER A PREFERENCE, and this was the other way
' round until the variant corpus measured it.
'
' A form feed separates page n from page n+1. It does not precede page ONE, so
' `break: formfeed` leaves the first page's header block in the document --
' every source then reports exactly one section too many, which looks like an
' off-by-one in section detection and is not. MEASURED on the five teller
' journals, whose pagination style IS uniform and so took the form-feed branch:
' region coverage 0/5 with `break: formfeed`, 5/5 with the header pattern, the
' same corpus and the same inference either way.
'
' Phase 2 recorded the opposite rule -- "where all of them paginate by form
' feed, formfeed is the better answer, since it cannot be defeated by a header
' whose wording drifts". The concern is real and the conclusion was wrong, and
' it went untested because the branch corpus is never uniform in pagination
' style, so the form-feed branch had never once been taken.
'
' So the header LINE is matched whenever there is a stable literal to match,
' which is every report that prints a title on each page. The form feed is used
' only when there is no such literal -- a report separated by form feeds alone
' -- and the caller is told that page one is not covered, because that is a
' fact about the specification and not an implementation detail.
function _page_block(f, formfeed_fallback)
    if count(f.offsets) = 0 then
        return []
    end if
    out = [ "page:" ]
    ' The first furniture line's own LITERAL words, anchored. Its typed spans --
    ' the page number and the run stamp -- are exactly what varies between
    ' pages, so a pattern built from the whole line would match page one only.
    lit = f.offsets[0].words
    if lit = "" then
        return _formfeed_block(f, formfeed_fallback)
    end if
    ' JOINED WITH `[ ]+`, NOT WITH A SPACE. `_words_of` normalises the gaps
    ' away, and a print-image header is column-aligned:
    ' `BRANCH ACTIVITY REGISTER                    PAGE    1`. A pattern built
    ' from the normalised words matched NOTHING, and the failure was silent --
    ' no furniture was stripped, so the page header became a section and every
    ' source reported more sections than it has.
    parts = []
    for each w in split(lit, " ")
        ' Alphanumeric words only: anything else would have to be escaped, and
        ' a header word that needs escaping is not a good anchor anyway.
        if is_unknown(match(w, regex("^[A-Za-z0-9]+$"))) then
            break
        end if
        append(parts, w)
    end for
    if count(parts) = 0 then
        return _formfeed_block(f, formfeed_fallback)
    end if
    append(out, "    break: /^" + join(parts, "[ ]+") + "/")
    append(out, "    drop: " + string(count(f.offsets)))
    append(out, "")
    return out
end function

function _formfeed_block(f, allowed)
    if not allowed then
        return []
    end if
    if f.evidence != "form feeds" then
        return []
    end if
    return [ "page:", "    break: formfeed",
             "    drop: " + string(count(f.offsets)), "" ]
end function

' §16 Phase 2, multi-source refinement: the `page:` block for a CORPUS.
'
' Returns the directive plus what it had to give up, because a caller deciding
' whether to trust a specification needs to know that the corpus disagreed and
' how it was resolved -- silently picking the portable form would hide a real
' fact about the data.
function corpus_furniture(profiles)
    styles = {}
    heights = {}
    for each p in profiles
        if count(p.furniture.offsets) > 0 then
            k = p.furniture.evidence
            if has(styles, k) then
                styles[k] = styles[k] + 1
            else
                styles[k] = 1
            end if
            h = string(count(p.furniture.offsets))
            if has(heights, h) then
                heights[h] = heights[h] + 1
            else
                heights[h] = 1
            end if
        end if
    end for
    if count(keys(styles)) = 0 then
        return { directive: [], styles: 0, heights: 0, alternatives: [],
                 why: "no source has identifiable page furniture" }
    end if
    ' A source with furniture to describe it from. Any will do for the literal,
    ' because the literal is what they share; the style is what they do not.
    pick = unknown
    for each p in profiles
        if is_unknown(pick) then
            if count(p.furniture.offsets) > 0 then
                pick = p
            end if
        end if
    end for
    uniform = count(keys(styles)) = 1
    d = _page_block(pick.furniture, uniform)
    note = ""
    ff = false
    for each ln in d
        if contains(ln, "break: formfeed") then
            ff = true
        end if
    end for
    if ff then
        note = ("no page carries a stable literal to break on, so the break is a"
                + " FORM FEED -- which separates page n from page n+1 and does"
                + " not precede page one, so the first page's furniture is NOT"
                + " stripped")
    else
        note = ("the break is matched on the header LINE rather than on a form"
                + " feed: a form feed does not precede page one, so a form-feed"
                + " break leaves the first page's furniture in the document")
        if not uniform then
            note = (note + ", and this corpus paginates two ways ("
                    + join(keys(styles), ", ") + ") in any case")
        end if
    end if
    if count(keys(heights)) > 1 then
        note = (note + ". The furniture block is not the same height in every"
                + " source (" + join(keys(heights), ", ") + " lines), and `drop:`"
                + " takes one value")
    end if
    ' §12: WHICH ALTERNATIVES WERE CONSIDERED. The form-feed directive is a real
    ' candidate whenever a source carries one, and recording why it lost is the
    ' difference between a rule and a rule a reader can audit -- this
    ' particular loser was the WINNER until the variant corpus measured it.
    alts = []
    if not ff then
        if has(styles, "form feeds") then
            append(alts, { kind: "page_break",
                           candidate: "break: formfeed",
                           supported_by: styles["form feeds"],
                           why_not: ("a form feed separates page n from page n+1"
                                     + " and does not precede page ONE, so the"
                                     + " first page's furniture would not be"
                                     + " stripped and every source would report"
                                     + " one section too many") })
        end if
    end if
    return { directive: d, styles: count(keys(styles)),
             heights: count(keys(heights)), why: note, alternatives: alts }
end function

' --- §8 anchor stability --------------------------------------------------
'
' THE MEASURE WITHOUT WHICH THE SCORECARD LIES, and it lied: a specification
' whose `columns` came from one source parsed all eight, produced exactly the
' 230 rows the generator planted, and reported `source_coverage 1.0` and
' `unknown_rate 0` -- while 104 of those 230 rows carried THE WRONG VALUES,
' because the corpus varies its table indent and a column rule cannot follow.
' Every value was an ordinary-looking account number and an ordinary-looking
' name. Nothing was unknown; nothing failed.
'
' So stability is checked WITHOUT the answer key, and directly rather than by
' comparing extracted values: a positional rule can only be stable if the
' family's COLUMN STRUCTURE is the same in every source. `gutters` already
' computes that from whitespace every row shares, so the check is to compute it
' per source and compare. It needs no extraction and cannot be fooled by a
' value that happens to look plausible in the wrong column.
' `profiles` may be supplied when the caller has already profiled the corpus.
' Recomputing is the same answer at several times the cost -- `furniture`
' searches for a period and `shape` is regex-heavy, so re-profiling every source
' twice more turned a 6-second fixture into one that timed out.
function anchor_stability(sources, fam_signature, options = nothing, profiles = nothing)
    o = _options(options)
    layouts = {}
    per = []
    si = 0
    for each s in sources
        if is_nothing(profiles) then
            g = grid(s.id, s.text)
            f = furniture(g, o)
            fams = families(g, f.lines, o)
        else
            g = profiles[si].grid
            fams = profiles[si].families
        end if
        si = si + 1
        found = unknown
        for each fm in fams
            if fm.signature = fam_signature then
                found = fm
            end if
        end for
        if is_unknown(found) then
            append(per, { id: s.id, layout: "(family absent)" })
            continue
        end if
        cols = gutters(g, found.lines)
        key = []
        for each c in cols
            append(key, string(c.cp_start) + "-" + string(c.cp_end))
        end for
        k = join(key, ",")
        append(per, { id: s.id, layout: k })
        if has(layouts, k) then
            layouts[k] = layouts[k] + 1
        else
            layouts[k] = 1
        end if
    end for
    top = 0
    topk = ""
    for each k in keys(layouts)
        if layouts[k] > top then
            top = layouts[k]
            topk = k
        end if
    end for
    n = count(sources)
    return { stability: top / n,
             layouts: count(layouts),
             dominant: topk,
             dominant_sources: top,
             sources: n,
             per_source: per }
end function

' --- the Phase 1 entry point ----------------------------------------------

' §8.2 HOLDOUT VALIDATION. `holdout: n` reserves the LAST n sources from
' inference and scores them separately.
'
' RESERVED FROM THE END, NOT AT RANDOM, because §17 criterion 9 requires the
' same proposal from the same ordered corpus: a random split would make the
' result depend on a seed nobody passed. A caller who wants a different split
' orders the corpus differently, which is a decision they can see.
'
' WHAT IT IS FOR is not reassurance. A specification inferred from a corpus and
' scored on the same corpus is scored on the data that shaped it, and the two
' numbers only part company when something has been fitted to the training set
' -- which is exactly when a reader needs to know.
' --- §10 PHASE 3: serializable decisions -----------------------------------
'
' §10: "Review decisions become explicit constraints for the next refinement
' pass. They should be serializable so inference can be reproduced." Both
' halves are load-bearing and the second is the harder one.
'
' A DECISION IS AN ORDINARY RECORD AND NOTHING ELSE -- no function values, no
' datetimes, nothing `encode` refuses -- so a decision list round-trips through
' a file and a later run reproduces the same proposal from the same corpus and
' the same decisions. That is §17 criterion 9 extended to the interactive path,
' and it is why a decision cannot be "a predicate the caller supplies": a
' predicate cannot be written down, so a proposal built with one could never be
' audited or reproduced.
'
' A DECISION THAT MATCHES NOTHING IS REFUSED, NEVER IGNORED, and this is the
' rule that matters most. A rename of a field that does not exist, silently
' dropped, leaves the reviewer believing they made a change they did not -- and
' the next thing they do is trust the specification. The message names the
' field and lists the fields that DO exist, because "which did you mean" is the
' reviewer's next question.
'
' WHAT IS DELIBERATELY NOT A DECISION. `allow_fixed_columns` is an OPTION and
' stays one: §10 lists "prefer or forbid positional extraction" among the
' decisions, but it already has a spelling, and two ways to say one thing is
' two things that can disagree. `refine` takes the options record beside the
' decisions, and a caller serialising a review serialises both.
'
' `field_type` CHANGES THE CONVERSION, NOT THE LOCATION. §10's "choose a type"
' is about a span that is plausibly two things -- `20260916` is a date and an
' identifier -- and in a generated rule that choice is the `as` clause. Moving a
' field is a different act with different evidence behind it, and pretending one
' decision does both would let a reviewer relocate a field by renaming its type.

function decision_kinds()
    return [ "variant", "row_family", "rename_field", "drop_field",
             "field_type" ]
end function

function decision_fields(kind)
    if kind = "variant" then
        return [ "decision", "sources" ]
    end if
    if kind = "row_family" then
        return [ "decision", "signature" ]
    end if
    if kind = "rename_field" then
        return [ "decision", "field", "to" ]
    end if
    if kind = "drop_field" then
        return [ "decision", "field" ]
    end if
    if kind = "field_type" then
        return [ "decision", "field", "as" ]
    end if
    return [ "decision" ]
end function

function conversion_names()
    return [ "text", "money", "date", "integer", "number" ]
end function

' Validated BY NAME, like the options record, and for the same reason: a
' misspelled key that were silently ignored would leave the reviewer believing
' they made a decision they did not.
function check_decisions(decisions)
    if is_nothing(decisions) then
        return []
    end if
    if type(decisions) != "array" then
        error ("ari_discover: decisions must be an array of records, not a "
               + type(decisions))
    end if
    kinds = decision_kinds()
    for each d in decisions
        if type(d) != "record" then
            error ("ari_discover: a decision must be a record, not a " + type(d))
        end if
        if not has(d, "decision") then
            error ("ari_discover: a decision must carry a `decision` field -- "
                   + "the kinds are " + join(kinds, ", "))
        end if
        if not contains(kinds, d.decision) then
            error ("ari_discover: '" + string(d.decision) + "' is not a decision"
                   + " -- the kinds are " + join(kinds, ", "))
        end if
        want = decision_fields(d.decision)
        for each k in keys(d)
            if not contains(want, k) then
                error ("ari_discover: '" + string(k) + "' is not a field of a "
                       + d.decision + " decision -- its fields are "
                       + join(want, ", "))
            end if
        end for
        for each k in want
            if not has(d, k) then
                error ("ari_discover: a " + d.decision + " decision needs `" + k
                       + "` -- its fields are " + join(want, ", "))
            end if
        end for
        if d.decision = "variant" then
            if type(d.sources) != "array" then
                error ("ari_discover: a variant decision names the SOURCES that"
                       + " form it, as an array of ids, not a " + type(d.sources))
            end if
            if count(d.sources) < 2 then
                error ("ari_discover: a variant of "
                       + string(count(d.sources)) + " source(s) is not a form,"
                       + " it is a sample -- §5.1: variation across files is"
                       + " what separates a true constant from an accidental"
                       + " one, so a specification from one source is a"
                       + " specification fitted to it")
            end if
        end if
        if d.decision = "field_type" then
            if not contains(conversion_names(), d.as) then
                error ("ari_discover: '" + string(d.as) + "' is not a conversion"
                       + " -- they are " + join(conversion_names(), ", "))
            end if
        end if
    end for
    return decisions
end function

' §9: revise a candidate using validation evidence and human decisions.
'
' It RE-INFERS rather than editing the proposal in place, deliberately. A
' decision changes what the evidence supports -- choose a different row family
' and the heading, the sections and every field change with it -- so patching
' the old proposal would leave a specification whose scorecard described a
' different one. `proposal` is accepted and not required, because the reviewer
' reached their decisions by reading it and the provenance should say so.
function refine(sources, proposal, decisions, options = nothing)
    o = _options(options)
    ' The proposal is CHECKED rather than merely accepted. Nothing below reads
    ' it -- a decision is applied to the evidence, not to the old record -- so
    ' an unchecked parameter would be decorative, and the mistake it exists to
    ' catch is a real one: `refine(sources, decisions, options)`, three
    ' arguments in the wrong places, would otherwise return a perfectly valid
    ' UNREFINED specification with nothing said.
    if not is_nothing(proposal) then
        if type(proposal) != "record" then
            error ("ari_discover.refine: the second argument is the proposal"
                   + " being revised, not a " + type(proposal)
                   + " -- refine(sources, proposal, decisions [, options])")
        end if
        if not has(proposal, "spec") then
            error ("ari_discover.refine: the second argument is the proposal"
                   + " being revised (a record from `infer`), and this one"
                   + " carries no `spec`"
                   + " -- refine(sources, proposal, decisions [, options])")
        end if
    end if
    ds = check_decisions(decisions)

    ' §10's "declare two apparent report forms to be distinct variants", which
    ' `variants` recommends and this is how a reviewer ACTS on. Functionally it
    ' narrows the corpus, and a caller could narrow it by hand -- but a subset
    ' passed by hand is not written down anywhere, and being written down is the
    ' whole of what §10 asks for. Order is preserved from the corpus so the
    ' proposal stays reproducible (§17 criterion 9).
    train = sources
    for each d in ds
        if d.decision = "variant" then
            kept = []
            for each sc in train
                if contains(d.sources, sc.id) then
                    append(kept, sc)
                end if
            end for
            if count(kept) != count(d.sources) then
                have = []
                for each sc in train
                    append(have, string(sc.id))
                end for
                missed = []
                for each want in d.sources
                    if not contains(have, string(want)) then
                        append(missed, string(want))
                    end if
                end for
                error ("ari_discover.refine: the variant names source(s) this"
                       + " corpus does not contain: " + join(missed, ", ")
                       + " -- it holds " + join(have, ", "))
            end if
            train = kept
        end if
    end for
    return _infer_from(train, [], o, nothing, ds)
end function

' --- §13 PHASE 3: variants ------------------------------------------------
'
' §13 asks whether a corpus holds several legitimate report GRAMMARS rather
' than one brittle specification. The hazard is the one Recipe 1 named and this
' library has already met twice: a search always returns a winner, so a
' variant detector pointed at a corpus that merely DRIFTS will happily report
' variants, and the split looks exactly like a discovery.
'
' THE EXISTING CORPUS IS THE NEGATIVE CONTROL AND IT WAS MEASURED FIRST. Its 24
' sources vary nine axes -- money notation, indent, date dialect, description
' width, the total's label, pagination style -- and Phase 2 measured that ONE
' refined specification recovers 121/121 branch numbers, totals and row counts
' across all of them. So a detector that split that corpus would be splitting
' on differences a single specification demonstrably carries.
'
' WHAT MAKES A DIFFERENCE MATERIAL IS NOT HOW BIG IT LOOKS. It is whether it
' changes what the specification must SAY, and that is derivable rather than
' chosen: the generator reads the pagination directive, the section labels and
' the detail row's own grammar. Of those, the first two are values inside
' locators -- a regex alternation carries them, which Phase 2 proved -- and the
' third is the rule's SHAPE, which no alternation can carry, because a field
' that does not exist in one form cannot be written into a rule shared with it.
'
'     branch detail   <IDENTIFIER> <WORD> <WORD> <DATE> <MONEY>
'     teller detail   <NUMBER> <WORD> <IDENTIFIER> <MONEY> <MONEY>
'
' Column layout is deliberately NOT a grouping axis: the default specification
' is anchor-relative and encodes no column, so two sources whose columns differ
' need no different specification. `anchor_stability` is where that difference
' is reported, and it is reported as a QUESTION rather than a split precisely
' because the remedy is a decision (§10).
'
' AND THE GROUPING IS ONLY A HYPOTHESIS. The recommendation is decided by
' RUNNING both arrangements and counting how many sources each serves -- a
' difference between two measured runs, not a property of the grouping. A
' corpus can hold two grammars that one specification still serves, and it can
' hold two groups neither of which has enough sources to support a
' specification worth believing; both are answers this returns, and neither is
' visible from the grouping alone.

function _dominant_family(p)
    best = unknown
    for each fm in p.families
        if is_unknown(best) then
            best = fm
        else
            if fm.count > best.count then
                best = fm
            end if
        end if
    end for
    return best
end function

' The source's grammar, as a string. The family key carries indentation
' (`families` needs it to separate a column heading from a remark note) and
' indentation is exactly what must NOT decide a variant, so it is stripped
' here.
function grammar_key(p)
    fm = _dominant_family(p)
    if is_unknown(fm) then
        return "<NONE>"
    end if
    parts = split(fm.shape, "|")
    if count(parts) < 2 then
        return fm.shape
    end if
    return parts[1]
end function

' How many of `srcs` a specification actually serves: it must parse the source
' AND find the number of sections the source's own detail rows say are there.
' Parsing alone is not service -- Phase 2 measured a specification with
' source_coverage 1.0 and region_coverage 0.125.
function _served(srcs, prop, options, profiles)
    if not prop.ok then
        return 0
    end if
    rc = region_coverage(srcs, prop.spec, prop.family.signature, options,
                         profiles)
    return rc.agreeing
end function

function variants(sources, options = nothing)
    o = _options(options)
    if type(sources) != "array" then
        error "ari_discover.variants expects an array of sources"
    end if
    if count(sources) = 0 then
        error "ari_discover.variants: the corpus is empty"
    end if
    c = profile_corpus(sources, o)

    keyed = {}
    order = []
    i = 0
    for each p in c.profiles
        k = grammar_key(p)
        if not has(keyed, k) then
            keyed[k] = []
            append(order, k)
        end if
        a = keyed[k]
        append(a, i)
        keyed[k] = a
        i = i + 1
    end for

    ' The group records carry the source IDS and not the sources. A returned
    ' record holding every source's text is a second copy of the corpus, which
    ' makes the answer expensive to keep, awkward to print and impossible to
    ' write down beside a decision. The members travel alongside, locally.
    groups = []
    group_members = []
    group_profiles = []
    for each k in order
        idxs = keyed[k]
        ids = []
        members = []
        profs = []
        for each ix in idxs
            append(ids, sources[ix].id)
            append(members, sources[ix])
            append(profs, c.profiles[ix])
        end for
        append(groups, { grammar: k, n: count(idxs), sources: ids })
        append(group_members, members)
        append(group_profiles, profs)
    end for

    ' The single-specification arrangement, measured rather than assumed.
    one = _infer_from(sources, [], o, c)
    one_served = _served(sources, one, o, c.profiles)

    small = []
    for each g in groups
        if g.n < o.minimum_variant_sources then
            append(small, g.grammar)
        end if
    end for

    if count(groups) = 1 then
        return { ok: true,
                 recommendation: "one",
                 groups: groups,
                 one: { served: one_served, sources: count(sources),
                        ok: one.ok, spec: _spec_or_blank(one) },
                 split: unknown,
                 why: ("every source in this corpus has the same detail row"
                       + " grammar (" + groups[0].grammar + "), so there is only"
                       + " one report form here. The differences between sources"
                       + " -- notation, layout, label wording, pagination -- are"
                       + " carried by one specification, which serves "
                       + string(one_served) + " of " + string(count(sources))
                       + " sources.") }
    end if

    ' BOTH ARRANGEMENTS ARE RUN BEFORE ANYTHING IS RECOMMENDED, including when
    ' the groups look too thin. The first version decided `none` versus
    ' `more_samples` by comparing group sizes to the floor, and that is a
    ' threshold answering a question only a measurement can: a corpus of six
    ' sources in two grammars of three, with the floor raised to four, came back
    ' `none` -- "no specification can be written against this corpus" -- when in
    ' fact each grammar supports one perfectly well and all the caller had said
    ' was that three samples is too few to CALL something a variant.
    '
    ' A group of ONE is not inferred from at all: §5.1's whole argument is that
    ' variation across files is what separates a true constant from an
    ' accidental one, so a specification from a single source is a specification
    ' fitted to it. Those groups are named and contribute nothing.
    per = []
    split_served = 0
    gi = 0
    for each g in groups
        if g.n < 2 then
            append(per, { grammar: g.grammar, sources: g.n, served: 0,
                          ok: false, spec: "" })
        else
            gm = group_members[gi]
            gc = profile_corpus(gm, o, group_profiles[gi])
            gp = _infer_from(gm, [], o, gc)
            gs = _served(gm, gp, o, group_profiles[gi])
            split_served = split_served + gs
            append(per, { grammar: g.grammar, sources: g.n, served: gs,
                          ok: gp.ok, spec: _spec_or_blank(gp) })
        end if
        gi = gi + 1
    end for

    if split_served = 0 then
        if one_served = 0 then
            ' NOTHING SERVES ANYTHING, which is a measured claim rather than an
            ' inference from how the sources grouped. This is what the null
            ' corpus produces: its sources do fall into grammars, and none of
            ' those grammars supports a specification either.
            return { ok: false,
                     recommendation: "none",
                     groups: groups,
                     one: { served: 0, sources: count(sources), ok: one.ok,
                            spec: _spec_or_blank(one) },
                     split: { served: 0, sources: count(sources),
                              per_group: per },
                     why: ("there is nothing here to split. "
                           + string(count(groups)) + " distinct detail grammars"
                           + " across " + string(count(sources)) + " sources, and"
                           + " NO arrangement serves a single source -- neither"
                           + " one specification for the corpus nor one per"
                           + " grammar. " + one.why) }
        end if
    end if

    if count(small) > 0 then
        return { ok: true,
                 recommendation: "more_samples",
                 groups: groups,
                 one: { served: one_served, sources: count(sources),
                        ok: one.ok, spec: _spec_or_blank(one) },
                 split: { served: split_served, sources: count(sources),
                          per_group: per },
                 why: ("this corpus holds " + string(count(groups))
                       + " distinct detail row grammars, but "
                       + string(count(small)) + " of them "
                       + _is_are(count(small)) + " carried by fewer"
                       + " than " + string(o.minimum_variant_sources)
                       + " sources (" + join(small, "; ") + "). Splitting would"
                       + " serve " + string(split_served) + " of "
                       + string(count(sources)) + " sources against "
                       + string(one_served) + ", so the split may well be right"
                       + " -- but a specification generated from one or two"
                       + " samples cannot be distinguished from one fitted to"
                       + " them, so what is needed is more samples of those"
                       + " forms rather than a decision (design §13).") }
    end if

    rec = "one"
    why = ("one specification serves " + string(one_served) + " of "
           + string(count(sources)) + " sources and "
           + string(count(groups)) + " separate ones serve "
           + string(split_served) + ", so the difference between these grammars"
           + " is one a single specification already carries. §13's first"
           + " remedy -- one specification with alternate sections -- applies.")
    if split_served > one_served then
        rec = "split"
        why = ("one specification serves " + string(one_served) + " of "
               + string(count(sources)) + " sources; one specification per"
               + " grammar serves " + string(split_served) + ". These are"
               + " materially different report forms, not one form with"
               + " variation in it, and the detail rows are where they differ:"
               + " " + join(_strs(_grammars_of(groups)), "  vs  "))
    end if

    return { ok: true,
             recommendation: rec,
             groups: groups,
             one: { served: one_served, sources: count(sources),
                    ok: one.ok, spec: _spec_or_blank(one) },
             split: { served: split_served, sources: count(sources),
                      per_group: per },
             why: why }
end function

function _section_pattern(sect)
    if is_unknown(sect) then
        return ""
    end if
    return sect.heading_pattern
end function

function _verb_of(kind)
    if kind = "rename_field" then
        return "rename"
    end if
    if kind = "drop_field" then
        return "drop"
    end if
    return "convert"
end function

function _is_are(n)
    if n = 1 then
        return "is"
    end if
    return "are"
end function

function _spec_or_blank(p)
    if p.ok then
        return p.spec
    end if
    return ""
end function

function _grammars_of(groups)
    out = []
    for each g in groups
        append(out, g.grammar)
    end for
    return out
end function

function infer(sources, options = nothing)
    o = _options(options)
    train = sources
    held = []
    if o.holdout > 0 then
        if o.holdout >= count(sources) then
            error ("ari_discover.infer: holdout of " + string(o.holdout)
                   + " leaves nothing to infer from (" + string(count(sources))
                   + " sources)")
        end if
        train = []
        i = 0
        while i < count(sources)
            if i < count(sources) - o.holdout then
                append(train, sources[i])
            else
                append(held, sources[i])
            end if
            i = i + 1
        end while
    end if
    return _infer_from(train, held, o)
end function

function _infer_from(sources, held, options = nothing, corpus_in = nothing, decisions = nothing)
    o = _options(options)
    ds = check_decisions(decisions)
    c = corpus_in
    if is_nothing(c) then
        c = profile_corpus(sources, o)
    end if

    ' The dominant family, across the corpus rather than within one source.
    ' §5.1: variation between files is what separates a true constant from an
    ' accidental one, so a family present in one source is not a candidate
    ' however many lines it holds there.
    best = unknown
    best_n = 0
    cands = []
    for each p in c.profiles
        ' The candidate must be DOMINANT within its own source, not merely
        ' recurring across the corpus.
        '
        ' MEASURED: requiring recurrence alone, `infer` proposed a
        ' specification for the NULL corpus. The family it chose was
        ' `<MONEY> <DATE>` -- ONE LINE in its source -- which appeared in 11 of
        ' 12 structureless sources by pure chance, because a two-token shape
        ' recurs whenever tokens are drawn at random. It cleared
        ' `minimum_support` and was the largest of the shared families, so it
        ' won.
        '
        ' §16 Phase 1 asks for "one DOMINANT repeating row family" and the first
        ' version implemented only "repeating". The share separates them by a
        ' wide margin: the real corpus's detail family holds 55% of its source's
        ' content lines, the null corpus's best holds 1.5%. The default sits
        ' between with room on both sides, and is an OPTION rather than a
        ' constant so a report whose detail rows are a genuinely small minority
        ' can say so.
        content = 0
        for each r in p.grid
            if not r.blank then
                if not contains(p.furniture.lines, r.physical_line) then
                    content = content + 1
                end if
            end if
        end for
        for each fm in p.families
            share = 0
            if content > 0 then
                share = fm.count / content
            end if
            if share >= o.minimum_family_share then
                shared = 0
                for each q in c.profiles
                    for each g in q.families
                        if g.signature = fm.signature then
                            shared = shared + 1
                        end if
                    end for
                end for
                if shared / c.sources >= o.minimum_support then
                    ' EVERY qualifying candidate is kept, not just the winner.
                    ' §12 asks which alternatives were considered, and a
                    ' `row_family` decision (§10) can only name one that was
                    ' recorded.
                    append(cands, { family: fm, profile: p, shared: shared,
                                    share: share })
                    if fm.count > best_n then
                        best_n = fm.count
                        best = { family: fm, profile: p, shared: shared,
                                 share: share }
                    end if
                end if
            end if
        end for
    end for

    if is_unknown(best) then
        ' A REFUSAL, not an empty proposal. Nothing recurs across the corpus, so
        ' there is nothing a specification could be written against -- which is
        ' §14's "insufficient variation" outcome, and is the RIGHT answer on a
        ' corpus with no structure in it.
        return { ok: false,
                 why: ("no row family is both DOMINANT within a source (at least "
                       + string(floor(o.minimum_family_share * 100))
                       + "% of its content lines) and present in at least "
                       + string(floor(o.minimum_support * 100)) + "% of the "
                       + string(c.sources) + " sources, so there is no repeating"
                       + " structure to write a specification against"),
                 spec: "", fields: [], questions: [], alternatives: [],
                 decisions: ds, section_fields: [], section_pattern: "",
                 trained_on: count(sources), holdout: unknown, corpus: c }
    end if

    ' §10: the reviewer may choose a different row family. Refused if it names
    ' a family no source carries, with the candidates listed -- silently
    ' falling back to the dominant one would produce exactly the specification
    ' the reviewer asked not to have.
    ' A THRESHOLD THAT EXISTS TO STOP THE TOOL GUESSING MUST NOT STOP A PERSON
    ' DECIDING. `minimum_family_share` and `minimum_support` are how automatic
    ' choice declines to guess; a reviewer naming a family has not guessed, and
    ' binding them to the candidate list would make the commonest reason for
    ' overriding -- "you picked the wrong family" -- unsayable exactly when the
    ' tool picked wrongly. So the decision reaches ANY family the corpus
    ' carries, and what it is refused for is naming one that is not there.
    chosen_by = "dominance"
    for each d in ds
        if d.decision = "row_family" then
            pick = unknown
            hits = 0
            for each p in c.profiles
                for each fm in p.families
                    if fm.signature = d.signature then
                        hits = hits + 1
                        if is_unknown(pick) then
                            pick = { family: fm, profile: p, shared: 0,
                                     share: 0 }
                        end if
                    end if
                end for
            end for
            if is_unknown(pick) then
                names = []
                for each p in c.profiles
                    for each fm in p.families
                        if not contains(names, fm.signature) then
                            append(names, fm.signature)
                        end if
                    end for
                end for
                error ("ari_discover.refine: no row family with signature '"
                       + string(d.signature) + "' appears anywhere in this"
                       + " corpus -- the families are: " + join(names, " | "))
            end if
            content2 = 0
            for each r in pick.profile.grid
                if not r.blank then
                    if not contains(pick.profile.furniture.lines, r.physical_line) then
                        content2 = content2 + 1
                    end if
                end if
            end for
            sh2 = 0
            if content2 > 0 then
                sh2 = pick.family.count / content2
            end if
            best = { family: pick.family, profile: pick.profile,
                     shared: hits, share: sh2 }
            chosen_by = "decision"
        end if
    end for

    ' §12: the candidates that were NOT chosen, deduplicated by signature.
    alternatives = []
    alt_seen = []
    for each cd in cands
        if cd.family.signature != best.family.signature then
            if not contains(alt_seen, cd.family.signature) then
                append(alt_seen, cd.family.signature)
                append(alternatives, { kind: "row_family",
                                       signature: cd.family.signature,
                                       lines: cd.family.count,
                                       share: cd.share,
                                       sources: cd.shared,
                                       why_not: ("it clears both thresholds but"
                                           + " holds " + string(cd.family.count)
                                           + " lines against the chosen family's "
                                           + string(best.family.count)) })
            end if
        end if
    end for

    inf = infer_fields(best.profile.grid, best.family,
                       best.profile.furniture.lines, o)

    ' §10: rename, drop and re-convert, applied AFTER the fields are inferred
    ' so a decision is made against the evidence the reviewer actually read.
    '
    ' APPLIED IN THE ORDER GIVEN, and that is what makes a review reproducible:
    ' a rename followed by a conversion must name the NEW name, and a rename
    ' after a drop is refused because the field is gone. Both follow from the
    ' list being a sequence rather than a set, which is also why the list is the
    ' serializable form -- a set would have to define its own ordering somewhere
    ' the reviewer cannot see.
    inf_fields = inf.fields
    for each d in ds
        if d.decision != "row_family" and d.decision != "variant" then
            hit = false
            out_f = []
            for each fl in inf_fields
                if fl.name = d.field then
                    hit = true
                    if d.decision = "rename_field" then
                        fl.name = d.to
                        append(out_f, fl)
                    end if
                    if d.decision = "field_type" then
                        if d.as = "text" then
                            fl.as_type = ""
                        else
                            fl.as_type = " as " + d.as
                        end if
                        append(out_f, fl)
                    end if
                    ' drop_field appends nothing.
                else
                    append(out_f, fl)
                end if
            end for
            if not hit then
                have = []
                for each fl in inf_fields
                    append(have, fl.name)
                end for
                error ("ari_discover.refine: this specification has no field '"
                       + string(d.field) + "' to " + _verb_of(d.decision)
                       + " -- its fields are " + join(have, ", "))
            end if
            inf_fields = out_f
        end if
    end for
    inf = { fields: inf_fields, questions: inf.questions,
            heading: inf.heading, columns: inf.columns }

    ' PHASE 2: the section that contains the rows, and the furniture directive
    ' that keeps the page header out of it.
    sc2 = sections(best.profile.grid, best.profile.families, best.family,
                   best.profile.furniture.lines, o)
    cl = corpus_labels(c.profiles, best.family.signature, o)
    cf = corpus_furniture(c.profiles)
    pagedir = cf.directive
    sect = unknown
    if not is_unknown(sc2.heading) then
        ' Fields of the heading line itself: its typed spans, located by their
        ' own kind relative to the literal that names the section.
        hfs = []
        hline = best.profile.grid[sc2.heading.lines[0] - 1].text
        seen2 = {}
        for each x in spans(hline)
            if x.kind = "number" then
                if not has(seen2, "number") then
                    seen2["number"] = 1
                    append(hfs, { name: lower(replace(sc2.heading.literal, " ", "_")) + "_no",
                                  locator: "right of \"" + sc2.heading.literal + "\"",
                                  as_type: " as integer",
                                  why: ("the number following \"" + sc2.heading.literal
                                        + "\", which names the section") })
                end if
            end if
        end for
        ' The patterns come from the CORPUS, the rest from the one source that
        ' supplied the columns. Which is which is recorded in the proposal.
        hp = sc2.heading.literal
        if count(cl.headings) > 1 then
            hp = label_token(cl.headings)
            hp = mid(hp, 1, len(hp) - 2)
        end if
        ' A SECTION NEED NOT HAVE A TOTAL. `sections` answers `unknown` when
        ' nothing consistently closes a run, and `spec_text_nested` already
        ' omits the field in that case -- but this line reached into it for a
        ' literal regardless, which only ever ran when the corpus had no total
        ' label at all. A `row_family` decision naming a family with no totals
        ' under it is the first thing that reaches it.
        tt = label_token(cl.totals)
        if tt = "" then
            if is_unknown(sc2.total) then
                tt = ""
            else
                tt = "\"" + string(sc2.total.literal) + "\""
            end if
        end if
        sect = { heading: sc2.heading, total: sc2.total, heading_fields: hfs,
                 heading_pattern: hp, total_token: tt, runs: sc2.runs,
                 labels: cl }
    end if
    ' THE SECTION'S OWN FIELDS, in the same shape as a detail field, so §12's
    ' account of the specification can reach every rule in it. They were
    ' invisible to `explain` until the tripwire that requires every `field`
    ' line to be explained found two that were not.
    section_fields = []
    if not is_unknown(sect) then
        for each hf in sect.heading_fields
            append(section_fields, { name: hf.name, locator: hf.locator,
                                     as_type: hf.as_type, why: hf.why,
                                     positional: false, type: "integer",
                                     consistency: 1,
                                     example: sect.heading.literal })
        end for
        if not is_unknown(sect.total) then
            if sect.total_token != "" then
                append(section_fields,
                       { name: "group_total",
                         locator: "right of " + sect.total_token,
                         as_type: " as money",
                         why: "the closing amount of the run, by its own label",
                         positional: false, type: "money", consistency: 1,
                         example: string(sect.total.literal) })
            end if
        end if
    end if
    for each a in cf.alternatives
        append(alternatives, a)
    end for
    if count(cl.totals) > 1 then
        append(alternatives, { kind: "section_total_label",
                               candidate: "\"" + string(cl.totals[0]) + "\"",
                               supported_by: 1,
                               why_not: ("the corpus uses "
                                   + string(count(cl.totals)) + " wordings for"
                                   + " this label, so a single literal describes"
                                   + " only some sources; the locator is an"
                                   + " alternation of all of them") })
    end if
    sp = spec_text_nested(best.family, inf, sect, pagedir, o)
    sc = validate(sources, sp, o)
    st = anchor_stability(sources, best.family.signature, o, c.profiles)
    rc = region_coverage(sources, sp, best.family.signature, o, c.profiles)

    positional = 0
    for each f in inf.fields
        if f.positional then
            positional = positional + 1
        end if
    end for
    pd = 0
    if count(inf.fields) > 0 then
        pd = positional / count(inf.fields)
    end if

    ' A POSITIONAL FIELD ON AN UNSTABLE LAYOUT IS THE HEADLINE, not a footnote.
    ' Every other number on the scorecard says the specification is fine, so
    ' this one has to be loud enough to be read first -- and it is a QUESTION
    ' (§10), because the remedy is a decision nobody here can make: fewer
    ' fields, or several specifications (§13 variants), or an anchor the report
    ' does not currently carry.
    qs = inf.questions
    if rc.coverage < 1 then
        append(qs, { field: "(the whole specification)",
                     kind: "regions",
                     example: string(rc.agreeing) + "/" + string(rc.sources),
                     why: ("this specification finds the right number of sections"
                           + " in only " + string(rc.agreeing) + " of "
                           + string(rc.sources) + " sources, while parsing all of"
                           + " them without error. It was built from one source"
                           + " (" + best.profile.id + ") and the corpus is not"
                           + " uniform: pagination style, the total's label and"
                           + " the column layout each differ between sources, and"
                           + " none of those differences makes a parse FAIL."),
                     options: [ "split the corpus into variants and generate one specification each (design §13)",
                                "keep only the fields and directives that hold across every source",
                                "supply the variations so one specification can carry alternates" ] })
    end if
    if positional > 0 then
        if st.stability < 1 then
            append(qs, { field: "(all positional fields)",
                         kind: "layout",
                         example: st.dominant,
                         why: ("this specification uses " + string(positional)
                               + " positional field(s), and the corpus does NOT"
                               + " have one layout: " + string(st.layouts)
                               + " distinct column structures across "
                               + string(st.sources) + " sources, the commonest"
                               + " covering only " + string(st.dominant_sources)
                               + ". A `columns` rule built from one of them"
                               + " reads the WRONG COLUMNS on the others, and"
                               + " does so silently -- the values are still"
                               + " plausible and nothing is unknown."),
                         options: [ "keep only the anchor-relative fields, and accept fewer",
                                    "split the corpus into variants and generate one specification each (design §13)",
                                    "supply an anchor so the field stops needing a column" ] })
        end if
    end if

    hs = unknown
    if count(held) > 0 then
        hv = validate(held, sp, o)
        hr = region_coverage(held, sp, best.family.signature, o)
        hs = { sources: count(held),
               source_coverage: hv.source_coverage,
               region_coverage: hr.coverage,
               rows: hv.rows,
               unknown_rate: hv.unknown_rate,
               regions_per_source: hr.per_source }
    end if

    return { ok: true,
             spec: sp,
             trained_on: count(sources),
             holdout: hs,
             ' §12: WHICH SOURCE THE RULES CAME FROM. The family is chosen
             ' across the corpus but the column boundaries and the heading are
             ' read from ONE source, so a caller checking a positional rule has
             ' to know which -- and without it the proposal cannot be audited
             ' at all.
             built_from: best.profile.id,
             family_chosen_by: chosen_by,
             decisions: ds,
             alternatives: alternatives,
             family_share: best.share,
             nested: not is_unknown(sect),
             sections: sc2,
             section_fields: section_fields,
             section_pattern: _section_pattern(sect),
             page_directive: pagedir,
             furniture_note: cf.why,
             labels: cl,
             family: best.family,
             shared_by: best.shared,
             fields: inf.fields,
             questions: qs,
             heading: inf.heading,
             scorecard: { source_coverage: sc.source_coverage,
                          sources: sc.sources,
                          parsed: sc.parsed,
                          rows: sc.rows,
                          cells: sc.cells,
                          unknown_rate: sc.unknown_rate,
                          positional_dependence: pd,
                          anchor_stability: st.stability,
                          layouts: st.layouts,
                          region_coverage: rc.coverage,
                          regions_per_source: rc.per_source,
                          fields_located: count(inf.fields),
                          fields_unlocatable: count(inf.questions),
                          failures: sc.failures,
                          content_chars: sc.content_chars,
                          claimed_chars: sc.claimed_chars,
                          content_coverage: sc.content_coverage,
                          content_coverage_is: sc.content_coverage_is,
                          claims: sc.claims,
                          collisions_involved: sc.collisions_involved,
                          collision_rate: sc.collision_rate,
                          collision_rate_is: sc.collision_rate_is,
                          not_computable: sc.not_computable,
                          not_computable_why: sc.not_computable_why },
             corpus: c }
end function

' --- §12 a human-readable profile, or a rule-by-rule account of a proposal --
'
' ONE FUNCTION, dispatching on whether it was handed a proposal or a profile.
' §9's API names one `explain`, the two records are disjoint in shape (only a
' proposal carries a `spec`), and "the thing to explain" is genuinely one idea:
' the alternative is two names a caller has to choose between for no reason
' they can see from the call site.
'
' WHAT A RULE EXPLANATION HAS TO ANSWER is §12's list, and the list is not
' decorative -- a generated specification is handed to somebody who will
' maintain it WITHOUT this library (design principle 8), so every rule has to
' carry which sources support it, what it anchors to, how often it succeeded,
' what was considered instead, whether an LLM proposed any of it, and whether it
' depends on a fixed column. A rule that cannot answer those is a rule nobody
' can safely change.
function explain(p, options = nothing)
    o = _options(options)
    if has(p, "spec") then
        return _explain_proposal(p, o)
    end if
    if not has(p, "id") then
        error ("ari_discover.explain takes a PROPOSAL (from `infer` or `refine`)"
               + " or a PROFILE (from `profile`, `profile_source`, or one of"
               + " `profile_corpus`'s), and this record is neither. A `variants`"
               + " result explains itself: read its `why`.")
    end if
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

function _explain_proposal(p, o)
    out = []
    if not p.ok then
        append(out, "NO SPECIFICATION WAS PROPOSED")
        append(out, "  " + p.why)
        return join(out, "\n")
    end if

    append(out, "specification inferred from " + string(p.trained_on) + " source(s)")
    append(out, "  built from    " + string(p.built_from)
                + "   (the column boundaries and the section heading are read"
                + " from this one source; the family and the label patterns come"
                + " from the corpus)")
    append(out, "  row family    " + p.family.signature)
    append(out, "                " + string(p.family.count) + " lines there, "
                + string(p.shared_by) + " of " + string(p.corpus.sources)
                + " sources carry it, " + string(floor(p.family_share * 100))
                + "% of its source's content lines")
    append(out, "  chosen by     " + p.family_chosen_by)
    ' §12 asks outright whether any part was proposed by an LLM. Answering it
    ' every time, including when the answer is no, is what makes the field
    ' worth reading -- a note that appears only sometimes is one a reader stops
    ' looking for.
    if is_nothing(o.llm) then
        append(out, "  llm           not used; every rule below is deterministic")
    else
        append(out, "  llm           an advisor was supplied")
    end if
    if count(p.decisions) > 0 then
        append(out, "  decisions     " + string(count(p.decisions)) + " applied")
        for each d in p.decisions
            append(out, "                " + _decision_text(d))
        end for
    end if

    append(out, "")
    append(out, "RULES")

    if count(p.page_directive) > 0 then
        append(out, "")
        append(out, "  " + trim(p.page_directive[1]))
        append(out, "      what      page furniture, stripped before anything else"
                    + " is located")
        append(out, "      evidence  " + string(p.corpus.sources)
                    + " source(s) profiled; " + p.furniture_note)
    end if

    if p.nested then
        append(out, "")
        append(out, "  section groups repeats starts(/^" + p.section_pattern + " /)")
        append(out, "      what      one section per run of detail rows")
        append(out, "      anchor    the literal \"" + p.sections.heading.literal
                    + "\", found by POSITION (what consistently precedes a run),"
                    + " never by vocabulary")
        append(out, "      evidence  " + string(p.sections.runs)
                    + " run(s) in the source it was built from; the heading"
                    + " literal was read from " + string(p.labels.sources)
                    + " source(s) carrying this family")
        append(out, "      succeeded " + string(p.scorecard.region_coverage * p.scorecard.sources)
                    + " of " + string(p.scorecard.sources)
                    + " sources report the number of sections their own detail"
                    + " rows say are there")
    end if

    allf = []
    for each f in p.section_fields
        append(allf, f)
    end for
    for each f in p.fields
        append(allf, f)
    end for
    for each f in allf
        append(out, "")
        append(out, "  field " + f.name + ": " + f.locator + f.as_type)
        append(out, "      what      " + f.why)
        append(out, "      anchor    " + _anchor_text(f))
        append(out, "      evidence  every row of the family agrees on the type"
                    + " (" + string(floor(f.consistency * 100)) + "% consistent);"
                    + " example " + f.example)
        append(out, "      columns   " + _positional_text(f))
    end for

    if count(p.questions) > 0 then
        append(out, "")
        append(out, "OPEN QUESTIONS (§10) -- " + string(count(p.questions)))
        for each q in p.questions
            append(out, "")
            append(out, "  " + string(q.field) + "   [" + string(q.kind) + "]")
            append(out, "      " + q.why)
            for each opt in q.options
                append(out, "      - " + opt)
            end for
        end for
    end if

    append(out, "")
    append(out, "ALTERNATIVES CONSIDERED (§12) -- " + string(count(p.alternatives)))
    if count(p.alternatives) = 0 then
        append(out, "  none: no other candidate cleared the thresholds")
    end if
    for each a in p.alternatives
        append(out, "")
        append(out, "  [" + a.kind + "] " + _alt_text(a))
        append(out, "      rejected: " + a.why_not)
    end for

    append(out, "")
    append(out, "SCORED BY `ari` ITSELF, never by the model that proposed it")
    append(out, "  sources parsed      " + string(p.scorecard.parsed) + " / "
                + string(p.scorecard.sources))
    append(out, "  sections right in   "
                + string(p.scorecard.region_coverage * p.scorecard.sources)
                + " / " + string(p.scorecard.sources))
    append(out, "  rows extracted      " + string(p.scorecard.rows))
    append(out, "  unknown cells       " + string(floor(p.scorecard.unknown_rate * 1000) / 10) + "%")
    append(out, "  anchor stability    "
                + string(floor(p.scorecard.anchor_stability * 100) / 100)
                + "   (" + string(p.scorecard.layouts) + " distinct column layouts)")
    append(out, "  positional fields   "
                + string(floor(p.scorecard.positional_dependence * 100)) + "%")
    for each nc in p.scorecard.not_computable
        append(out, "  " + _pad_right(nc, 20) + "not computable")
    end for
    append(out, "                      " + p.scorecard.not_computable_why)
    return join(out, "\n")
end function

function _decision_text(d)
    if d.decision = "row_family" then
        return "use the row family " + string(d.signature)
    end if
    if d.decision = "rename_field" then
        return "rename " + string(d.field) + " to " + string(d.to)
    end if
    if d.decision = "drop_field" then
        return "drop the field " + string(d.field)
    end if
    return "convert " + string(d.field) + " as " + string(d.as)
end function

function _anchor_text(f)
    if f.positional then
        return ("none -- this field is located by COLUMN " + string(f.cp_start)
                + "-" + string(f.cp_start + f.cp_length - 1))
    end if
    return ("the " + f.type + " span itself; the row carries no literal to"
            + " anchor to, so the locator counts spans of a type")
end function

function _positional_text(f)
    if f.positional then
        return ("YES -- it breaks silently if the report drifts, which is why"
                + " allow_fixed_columns defaults to false")
    end if
    return "no -- it survives a change of indent or column width"
end function

function _alt_text(a)
    if has(a, "signature") then
        return (string(a.signature) + "   " + string(a.lines) + " lines, "
                + string(a.sources) + " source(s)")
    end if
    return string(a.candidate)
end function

function _pad_right(s, w)
    n = w - len(s)
    if n <= 0 then
        return s
    end if
    return s + repeat(" ", n)
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
