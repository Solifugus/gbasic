' ari_discover Phase 0 -- SCORED AGAINST THE CORPUS'S OWN ANSWER KEY.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced twice over.
'
' Every defect this layer can have produces A PLAUSIBLE PROFILE. A furniture
' detector that claims one line too many still reports a tidy page header. A
' family grouping that fragments still reports families, just more of them --
' and the first draft did exactly that, 39 families in a report with eight, with
' nothing raised. A golden would have recorded either as expected.
'
' And the numbers here are MEASUREMENTS, not transcripts: precision and recall
' against `truth.json`, which was written by the generator from the values it
' planted rather than read back out of the text it printed.

load ari_discover
load ari

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print ("ok   " + label)
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
    return nothing
end function

function check_at_least(label, got, floor_v)
    tally.checks = tally.checks + 1
    if got >= floor_v then
        print ("ok   " + label + " (" + string(got) + " >= " + string(floor_v) + ")")
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want at least " + string(floor_v))
    end if
    return nothing
end function

function check_at_most(label, got, cap)
    tally.checks = tally.checks + 1
    if got <= cap then
        print ("ok   " + label + " (" + string(got) + " <= " + string(cap) + ")")
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want at most " + string(cap))
    end if
    return nothing
end function

function slurp(path)
    f {file}= path
    return read(f)
end function

function round3(x)
    return floor(x * 1000 + 0.5) / 1000
end function

dir = "examples/fixtures/ari_discover/"
truth = decode(slurp(dir + "truth.json"))

' ===========================================================================
print "-- FURNITURE: precision and recall against the planted answer key"
' ===========================================================================
' The load-bearing tier, and it is a PAIR. Recall alone is maximised by calling
' every line furniture, which would strip the whole report; precision alone by
' calling nothing furniture, which is what the first draft did on more than half
' the corpus and reported as a clean empty result. Both, or neither means
' anything.
hit = 0
claimed = 0
planted = 0
perfect = 0
multi = 0
single = 0
single_claimed = 0
single_refused = 0
for each src in truth.sources
    g = ari_discover.grid(src.id, slurp(dir + src.file))
    f = ari_discover.furniture(g)
    if src.pages >= 2 then
        multi = multi + 1
        want = src.furniture_lines
        h = 0
        for each ln in f.lines
            if contains(want, ln) then
                h = h + 1
            end if
        end for
        hit = hit + h
        claimed = claimed + count(f.lines)
        planted = planted + count(want)
        if count(f.lines) = count(want) then
            if h = count(want) then
                perfect = perfect + 1
            end if
        end if
    else
        ' A SINGLE-PAGE SOURCE IS THE OTHER HALF OF THE CLAIM, and the truth
        ' records furniture in it that IS NOT RECOVERABLE -- a header that
        ' appears once is indistinguishable from a first heading. What is
        ' required here is the REFUSAL, with a reason.
        single = single + 1
        single_claimed = single_claimed + count(f.lines)
        if f.why != "" then
            single_refused = single_refused + 1
        end if
    end if
end for
precision = hit / claimed
recall = hit / planted
print ("     " + string(multi) + " multi-page sources: " + string(planted)
       + " furniture lines planted, " + string(claimed) + " claimed")
print ("     " + string(single) + " single-page sources: "
       + string(single_claimed) + " claimed (the right answer is a refusal)")
check("furniture precision is 1.0", round3(precision), 1)
check("furniture recall is 1.0", round3(recall), 1)
check("every multi-page source is exactly right", perfect, multi)
check_at_least("the corpus is mostly multi-page, so the tier has samples", multi, 12)

' THE REFUSAL IS ASSERTED, NOT MERELY ALLOWED. Without this, "precision is 1.0"
' is satisfied by a detector that claims nothing anywhere -- which is exactly
' what an earlier draft did on 14 of 18 sources while reporting a clean result.
check("a single-page source claims nothing", single_claimed, 0)
check("and says WHY rather than returning an empty result", single_refused, single)
check_at_least("and there are single-page sources to refuse on", single, 1)

' ===========================================================================
print ""
print "-- FAMILIES: the detail rows are ONE family, and it is the planted count"
' ===========================================================================
' A family count alone is satisfied by a grouping that fragments (39 families
' where there are 8) AND by one that over-merges. The planted detail count is
' what distinguishes them: it is a number from the generator, not from us.
bad_family = 0
for each src in truth.sources
    g = ari_discover.grid(src.id, slurp(dir + src.file))
    f = ari_discover.furniture(g)
    fams = ari_discover.families(g, f.lines)
    want_detail = src.families.detail
    ' The detail family is the one whose signature is an identifier, two free
    ' text spans, a date and money -- stated here rather than taken as
    ' "the largest", because "the largest family has the right count" is also
    ' true of a grouping that merged two families into one.
    got = 0
    for each fm in fams
        if fm.signature = "<IDENTIFIER> <TEXT> <TEXT> <DATE> <MONEY>" then
            got = got + fm.count
        end if
    end for
    if got != want_detail then
        bad_family = bad_family + 1
        print ("     " + src.id + ": detail family " + string(got)
               + ", planted " + string(want_detail))
    end if
end for
check("every source recovers its planted detail-row count", bad_family, 0)

' The anchors a later phase would build a rule from must survive as LITERALS.
' This is the check the first draft failed silently: every word kept verbatim
' made each detail row its own family and destroyed `ACCT` as an anchor by
' burying it among five hundred one-line families.
g1 = ari_discover.grid("s1", slurp(dir + "01_branch_activity.rpt"))
f1 = ari_discover.furniture(g1)
fam1 = ari_discover.families(g1, f1.lines)
sigs = []
for each fm in fam1
    append(sigs, fm.signature)
end for
check("the total row keeps its anchor", contains(sigs, "BRANCH TOTAL <MONEY>"), true)
check("the column heading keeps its anchors",
      contains(sigs, "ACCT MEMBER NAME POSTED AMOUNT"), true)
check("the detail row has NO literal in it",
      contains(sigs, "<IDENTIFIER> <TEXT> <TEXT> <DATE> <MONEY>"), true)
check_at_most("and the whole source is a handful of families, not one per line",
              count(fam1), 12)

' ===========================================================================
print ""
print "-- INDEX SPACES: codepoints for rules, bytes for provenance (§6.1)"
' ===========================================================================
' THE CORPUS CANNOT CATCH THIS -- every file in it is ASCII, so the two spaces
' coincide exactly. This tier is the only thing standing between the library and
' the defect finio Phase 0 shipped: offsets accumulated with `len`, sliced with
' `mid`, and called `byte_offset`.
adv = ari_discover.grid("adv", slurp(dir + "adversarial/non_ascii.rpt"))
cp_cols = []
byte_cols = []
for each r in adv
    for each sp in ari_discover.spans(r.text)
        if sp.kind = "money" then
            if r.physical_line >= 9 then
                if r.physical_line <= 13 then
                    append(cp_cols, sp.cp_start + sp.cp_length)
                    append(byte_cols, sp.byte_start + sp.byte_length)
                end if
            end if
        end if
    end for
end for
check("five detail amounts found", count(cp_cols), 5)
' The report is column-aligned IN CODEPOINTS, as a real one is: the amounts are
' right-aligned to one column. So the codepoint ends agree and the byte ends do
' not -- asserted as a DIFFERENCE, because "we report two numbers" is satisfied
' by a library that reports the same number twice.
check("the amounts end at ONE codepoint column", count(unique(cp_cols)), 1)
check_at_least("and at MORE THAN ONE byte column", count(unique(byte_cols)), 2)
' And the direction: bytes are never fewer than codepoints.
shorter = 0
i = 0
while i < count(cp_cols)
    if byte_cols[i] < cp_cols[i] then
        shorter = shorter + 1
    end if
    i = i + 1
end while
check("no byte position is left of its codepoint position", shorter, 0)

' THE CONSEQUENCE, stated as the thing a caller would actually do: an `ari`
' `columns` rule built from the CODEPOINT column reads every row; one built from
' a byte column would not. `ari` is the judge, so this is checked by running it.
sp_lines = []
append(sp_lines, "section report:")
append(sp_lines, "    section rows repeats starts(/^  [0-9]{8}/):")
append(sp_lines, "        field amount: last money")
r = ari.parse(slurp(dir + "adversarial/non_ascii.rpt"), join(sp_lines, "\n"))
check("ari parses the non-ASCII source", r.ok, true)
got_amounts = 0
for each row in r.value.rows
    if not is_unknown(row.amount) then
        got_amounts = got_amounts + 1
    end if
end for
check("and extracts all five amounts through it", got_amounts, 5)

' ===========================================================================
print ""
print "-- DATE DIALECTS: half the corpus is DD-MMM-YYYY"
' ===========================================================================
' Building this corpus is what found that `ari` could not read the classic
' mainframe date at all. The recognizer tables are SHARED, so a date discovery
' reports is a date the engine can extract -- checked by doing both.
dmmm = 0
mdy = 0
for each src in truth.sources
    if src.variant.date_style = "dmmmy" then
        dmmm = dmmm + 1
    else
        mdy = mdy + 1
    end if
end for
check_at_least("the corpus carries DD-MMM-YYYY sources", dmmm, 8)
check_at_least("and numeric-date sources", mdy, 8)
' One of each, profiled and then parsed.
for each style in [ "dmmmy", "mdy" ]
    pick = ""
    for each src in truth.sources
        if pick = "" then
            if src.variant.date_style = style then
                pick = src.file
            end if
        end if
    end for
    gg = ari_discover.grid(style, slurp(dir + pick))
    seen_date = false
    for each row in gg
        for each sp in ari_discover.spans(row.text)
            if sp.kind = "date" then
                seen_date = true
            end if
        end for
    end for
    check(style + ": discovery recognises the date", seen_date, true)
    rr = ari.parse(slurp(dir + pick), join(sp_lines, "\n"))
    check(style + ": and ari parses the source", rr.ok, true)
end for

' ===========================================================================
print ""
print "-- NULL CORPUS: the right answer is nothing (§15.5)"
' ===========================================================================
' Criteria that only ever run on a corpus WITH structure are all satisfied by a
' tool that always finds something. This is the tier that is not.
ndir = dir + "null/"
ntruth = decode(slurp(ndir + "truth.json"))
null_furniture = 0
null_max_family = 0
null_families = 0
null_sources = 0
for each src in ntruth.sources
    g = ari_discover.grid(src.id, slurp(ndir + src.file))
    f = ari_discover.furniture(g)
    null_furniture = null_furniture + count(f.lines)
    fams = ari_discover.families(g, f.lines)
    null_families = null_families + count(fams)
    biggest = 0
    total = 0
    for each fm in fams
        total = total + fm.count
        if fm.count > biggest then
            biggest = fm.count
        end if
    end for
    if total > 0 then
        share = biggest / total
        if share > null_max_family then
            null_max_family = share
        end if
    end if
    null_sources = null_sources + 1
end for
print ("     " + string(null_sources) + " structureless sources: "
       + string(null_furniture) + " furniture lines claimed, "
       + string(null_families) + " families, largest holding "
       + string(round3(null_max_family * 100)) + "% of one source's lines")
check("no furniture is claimed where there is none", null_furniture, 0)

' AND THE CONTROL, without which "it found nothing" is satisfied by a library
' that finds nothing anywhere. The structured corpus must be the opposite on
' the same measure.
g1b = ari_discover.grid("s1", slurp(dir + "01_branch_activity.rpt"))
f1b = ari_discover.furniture(g1b)
fam1b = ari_discover.families(g1b, f1b.lines)
big = 0
tot = 0
for each fm in fam1b
    tot = tot + fm.count
    if fm.count > big then
        big = fm.count
    end if
end for
structured_share = big / tot
print ("     against the structured corpus: largest family holds "
       + string(round3(structured_share * 100)) + "% of one source's lines")
check_at_least("a structured source HAS a dominant family", round3(structured_share), 0.4)
check_at_most("a structureless one does not", round3(null_max_family), 0.25)

' ===========================================================================
print ""
print "-- DETERMINISM (§17 criterion 9)"
' ===========================================================================
a = ari_discover.profile(slurp(dir + "03_branch_activity.rpt"))
b = ari_discover.profile(slurp(dir + "03_branch_activity.rpt"))
check("the same source profiles identically", ari_discover.explain(a),
      ari_discover.explain(b))
check("and the family lists compare equal", a.families = b.families, true)

' ===========================================================================
print ""
print "-- REFUSALS, each beside its nearest legal neighbour"
' ===========================================================================
on error goto next
p = ari_discover.profile(slurp(dir + "01_branch_activity.rpt"), { minimum_suport: 0.9 })
if error then
    check("a misspelled option is refused BY NAME",
          contains(error.message, "minimum_suport"), true)
    error.clear()
else
    check("a misspelled option is refused", "accepted", "refused")
end if
on error stop

' The control: the correctly spelled one is accepted and CHANGES THE ANSWER,
' without which "options are validated" is satisfied by a library that ignores
' them all.
loose = ari_discover.profile(slurp(dir + "01_branch_activity.rpt"), { minimum_support: 0.2 })
tight = ari_discover.profile(slurp(dir + "01_branch_activity.rpt"), { minimum_support: 1.0 })
check("a correctly spelled option is accepted", count(loose.families) > 0, true)

on error goto next
p2 = ari_discover.profile(slurp(dir + "01_branch_activity.rpt"), 0.9)
if error then
    check("a non-record options argument is refused", contains(error.message, "record"), true)
    error.clear()
else
    check("a non-record options argument is refused", "accepted", "refused")
end if
on error stop

' Omitting options entirely is legal -- the default-parameter form.
d = ari_discover.profile(slurp(dir + "01_branch_activity.rpt"))
check("options may be omitted", count(d.families) > 0, true)
check("and omitting them is the same as passing the defaults",
      ari_discover.explain(d),
      ari_discover.explain(ari_discover.profile(slurp(dir + "01_branch_activity.rpt"),
                                                ari_discover.default_options())))

' ===========================================================================
print ""
print "-- SUPPORT WARNING: a threshold that cannot be calibrated says so"
' ===========================================================================
few = []
i = 0
while i < 3
    append(few, { id: truth.sources[i].id, text: slurp(dir + truth.sources[i].file) })
    i = i + 1
end while
cp = ari_discover.profile_corpus(few)
check("three sources cannot calibrate support", cp.support_warning != "", true)
many = []
i = 0
while i < 12
    append(many, { id: truth.sources[i].id, text: slurp(dir + truth.sources[i].file) })
    i = i + 1
end while
cp2 = ari_discover.profile_corpus(many)
check("twelve can", cp2.support_warning, "")
check_at_least("and a shared signature is found across them",
               count(cp2.shared_signatures), 1)

' ===========================================================================
print ""
print "-- PHASE 1: a generated specification, judged by `ari` (§18 principle 4)"
' ===========================================================================
' Nothing here scores a candidate from the model that produced it. Every number
' comes from running the spec through ordinary `ari.parse`.

corpus = []
planted_rows = 0
ci = 0
while ci < 8
    append(corpus, { id: truth.sources[ci].id, text: slurp(dir + truth.sources[ci].file) })
    for each b in truth.sources[ci].branches
        planted_rows = planted_rows + count(b.accounts)
    end for
    ci = ci + 1
end while

rel = ari_discover.infer(corpus)
pos = ari_discover.infer(corpus, { allow_fixed_columns: true })

check("a specification is proposed", rel.ok, true)
check("and `ari` parses every source with it", rel.scorecard.source_coverage, 1)
check("the same, with positional fields permitted", pos.scorecard.source_coverage, 1)

' THE STRONG ORACLE (§15.4): an accepted specification, run through ordinary
' `ari`, must recover THE PLANTED VALUES. A coverage percentage cannot
' substitute -- a specification can claim every line and extract the wrong
' number, which is exactly what the positional half does below.
function recovered(spec, corpus, truth, field, want_cents)
    ok = 0
    k = 0
    while k < count(corpus)
        r = ari.parse(corpus[k].text, spec)
        ' The specification is NESTED now, so the detail rows live under the
        ' sections. Flattened in source order, which is what the planted rows
        ' are in.
        flat = []
        if has(r.value, "rows") then
            for each x in r.value.rows
                append(flat, x)
            end for
        end if
        if has(r.value, "groups") then
            for each gp in r.value.groups
                if has(gp, "rows") then
                    for each x in gp.rows
                        append(flat, x)
                    end for
                end if
            end for
        end if
        idx = 0
        for each b in truth.sources[k].branches
            for each a in b.accounts
                if idx < count(flat) then
                    g = flat[idx]
                    if has(g, field) then
                        if not is_unknown(g[field]) then
                            if want_cents then
                                ' Compared in CENTS: exact, and free of how
                                ' money renders (44.90 against 44.9 is a
                                ' rendering difference, not a wrong value).
                                if floor(number(string(g[field])) * 100 + 0.5) = a.amount_cents then
                                    ok = ok + 1
                                end if
                            else
                                if string(g[field]) = a.account then
                                    ok = ok + 1
                                end if
                            end if
                        end if
                    end if
                end if
                idx = idx + 1
            end for
        end for
        k = k + 1
    end while
    return ok
end function

amt_rel = recovered(rel.spec, corpus, truth, "amount", true)
amt_pos = recovered(pos.spec, corpus, truth, "amount", true)
acct_pos = recovered(pos.spec, corpus, truth, "acct_no", false)

print ("     " + string(planted_rows) + " planted rows across " + string(count(corpus)) + " sources")
print ("     anchor-relative `amount`: " + string(amt_rel)
       + "   positional `acct_no`: " + string(acct_pos))

check("an anchor-relative field recovers EVERY planted value", amt_rel, planted_rows)
check("and does so whether or not positional rules are permitted", amt_pos, planted_rows)

' THE LOAD-BEARING PAIR, and it is a DIFFERENCE. "The generated spec works" is
' satisfied by the anchor-relative half alone; "positional rules are fragile" is
' a claim nobody measured until now. The corpus varies its table indent across
' nine declared axes, so a `columns` rule built from one source reads the wrong
' columns on the others -- and does it SILENTLY: source_coverage is 1.0 and
' unknown_rate is 0 while a third of the values are wrong.
check_at_most("a positional field recovers materially FEWER",
              acct_pos, planted_rows - 50)
check_at_least("but not none, or the comparison would be about something else",
               acct_pos, 1)
check("and the scorecard does not flag it as a failure -- coverage is still 1.0",
      pos.scorecard.source_coverage, 1)
check("nor as unknowns", pos.scorecard.unknown_rate, 0)

' WHICH IS WHY anchor_stability EXISTS. It is the one measure that catches it,
' and it is computed WITHOUT the answer key -- from whether the family's column
' structure is the same in every source.
check_at_most("anchor_stability sees it", round3(pos.scorecard.anchor_stability), 0.5)
check_at_least("and names how many distinct layouts there are", pos.scorecard.layouts, 2)

' A CONTROL: a corpus of ONE source has one layout, so stability is 1. Without
' this, "stability is low" is satisfied by a measure that is always low.
one = [ corpus[0] ]
st1 = ari_discover.anchor_stability(one, rel.family.signature)
check("a single-source corpus is perfectly stable", st1.stability, 1)

' THE COLUMN BOUNDARIES ARE GUTTERS, and this is what asserts it. Checked on
' the ONE source the specification was built from, which isolates the gutter
' question from the layout-drift question above: on that source a positional
' rule is exactly right, so any shortfall is the column boundary being wrong.
'
' Two defects were measured here before the gutter rule existed, and BOTH were
' silent: `columns 4-11` on an account column starting at 2 returned `147454`,
' the leading zeros gone; and a member name came back `YES, YUKI` because the
' widest observed surname still did not reach the column's right edge. A single
' space is NOT a gutter -- `REYES, YUKI` has one inside one value, and its
' position moves with the surname's length, which is what the all-rows test
' settles.
' The specification's column boundaries and heading come from ONE source, and
' the proposal says which. Using the wrong one here would test the drift rather
' than the gutters.
built_from = corpus[0]
for each cs in corpus
    if cs.id = pos.built_from then
        built_from = cs
    end if
end for
check("the proposal names the source its rules came from",
      pos.built_from != "", true)
r0 = ari.parse(built_from.text, pos.spec)
flat0 = []
if has(r0.value, "rows") then
    for each x in r0.value.rows
        append(flat0, x)
    end for
end if
if has(r0.value, "groups") then
    for each gp in r0.value.groups
        if has(gp, "rows") then
            for each x in gp.rows
                append(flat0, x)
            end for
        end if
    end for
end if
name_ok = 0
acct_ok0 = 0
n0 = 0
idx0 = 0
ti = 0
tsrc = truth.sources[0]
for each ts in truth.sources
    if ts.id = pos.built_from then
        tsrc = ts
    end if
end for
for each b in tsrc.branches
    for each a in b.accounts
        n0 = n0 + 1
        if idx0 < count(flat0) then
            g0 = flat0[idx0]
            if string(g0.member_name) = a.name then
                name_ok = name_ok + 1
            end if
            if string(g0.acct_no) = a.account then
                acct_ok0 = acct_ok0 + 1
            end if
        end if
        idx0 = idx0 + 1
    end for
end for
print ("     on the source it was built from: " + string(n0) + " rows")
check("the text column is WHOLE -- surname and forename in one field",
      name_ok, n0)
check("and the identifier keeps its leading zeros", acct_ok0, n0)

' A ONE-SPACE GAP IS NOT A GUTTER, and this is the only shape that can show it.
' In the main corpus a space inside a value moves with the surname's length, so
' the all-rows test rejects it for free and the `>= 2` rule never bites --
' measured, by perturbing it and watching nothing change. It bites when every
' value in a column is THE SAME WIDTH, because then the interior space is at a
' constant position and is indistinguishable from a gutter by recurrence alone.
'
' `adversarial/fixed_width_gap.rpt` is that case: operator codes `AB 1234`, all
' seven characters. With the rule, one column; without it, the code splits in
' two and a caller gets `AB` where the source said `AB 1234`.
fw = ari_discover.grid("fw", slurp(dir + "adversarial/fixed_width_gap.rpt"))
fwf = ari_discover.furniture(fw)
fwfam = unknown
for each fm in ari_discover.families(fw, fwf.lines)
    if fm.count >= 5 then
        fwfam = fm
    end if
end for
check("the fixed-width family is found", is_unknown(fwfam), false)
fwcols = ari_discover.gutters(fw, fwfam.lines)
check("a constant interior space does not split the column", count(fwcols), 4)
whole = trim(mid(fw[fwfam.lines[0] - 1].text, fwcols[1].cp_start,
                 fwcols[1].cp_end - fwcols[1].cp_start + 1))
check("and the value survives whole", whole, "AB 1234")

' §10: what cannot be located is a QUESTION with evidence, never a silent
' omission.
check_at_least("the default mode asks about what it cannot locate",
               count(rel.questions), 2)
check("each question carries its options", count(rel.questions[0].options) > 0, true)
check("and the positional run asks about the LAYOUT instead",
      contains(string(pos.questions), "distinct column structures"), true)

' Field names come from the column heading above the family (§3).
names = []
for each f in rel.fields
    append(names, f.name)
end for
check("fields are named from the heading, not positionally",
      contains(names, "amount"), true)
check("and the date column too", contains(names, "posted"), true)

' §8: two measures are NOT COMPUTABLE without ari's span-level surface, and are
' reported as unknown rather than estimated from the model that produced the
' spec -- which would be the tool grading its own homework.
check("content_coverage is reported unknown", is_unknown(rel.scorecard.content_coverage), true)
check("collision_rate is reported unknown", is_unknown(rel.scorecard.collision_rate), true)
check("and the reason names the limitation",
      contains(rel.scorecard.not_computable_why, "span-level"), true)

' ===========================================================================
print ""
print "-- PHASE 2: sections, nesting, and the furniture directive"
' ===========================================================================
check("the proposal is nested", rel.nested, true)
check("it found one section per run of detail rows",
      rel.sections.runs > 1, true)
check("the section heading is the one that VARIES, not the column caption",
      rel.sections.heading.literal, "BRANCH")
check("and a `page:` directive was generated from what Phase 0 measured",
      count(rel.page_directive) > 0, true)
check("the spec carries it", contains(rel.spec, "page:"), true)
check("and it nests the rows inside the section",
      contains(rel.spec, "section groups repeats"), true)

' ON THE SOURCE IT WAS BUILT FROM the structure is exactly right. This is the
' CONTROL for everything below: without it, "region coverage is low" is
' satisfied by a specification that is simply wrong, and the finding would be
' about incompetence rather than about heterogeneity.
bt = unknown
for each r in rel.scorecard.regions_per_source
    if r.id = rel.built_from then
        bt = r
    end if
end for
check("on its own source the section count is exact", bt.found, bt.wanted)

' THE PHASE 2 HEADLINE, and it is a DIFFERENCE between two measures of the same
' run. A specification can parse EVERY source without error and be structurally
' wrong in nearly all of them: pagination style, the total's label and the
' column layout each differ across the corpus, and none of those differences
' makes a parse fail.
print ("     source_coverage " + string(rel.scorecard.source_coverage)
       + "   region_coverage " + string(rel.scorecard.region_coverage))
check("every source parses", rel.scorecard.source_coverage, 1)
check_at_most("while the section count is right in a minority of them",
              round3(rel.scorecard.region_coverage), 0.5)
check("and a question names the cause",
      contains(string(rel.questions), "not uniform"), true)
check("which points at variants rather than at a cleverer rule",
      contains(string(rel.questions), "variants"), true)

' The per-source detail is reported, not just the fraction: an operator has to
' know WHICH sources disagree.
check("every source is accounted for individually",
      count(rel.scorecard.regions_per_source), count(corpus))

' ===========================================================================
print ""
print "-- PHASE 1 ON THE NULL CORPUS: the right answer is a refusal"
' ===========================================================================
' The tier that a confident guesser cannot pass. Phase 0 claims no furniture
' there; Phase 1 must decline to propose a specification at all.
nulls = []
for each src in ntruth.sources
    append(nulls, { id: src.id, text: slurp(ndir + src.file) })
end for
nres = ari_discover.infer(nulls)
check("no specification is proposed for structureless sources", nres.ok, false)
check("and it says why", contains(nres.why, "no row family"), true)
check("the spec is empty rather than a plausible-looking one", nres.spec, "")

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))
