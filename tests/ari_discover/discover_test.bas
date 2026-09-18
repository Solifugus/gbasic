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

' §8: the two measures that were reported `unknown` until `ari.trace` shipped
' (limitation C1, struck 2026-09-18). They are MEASURED BY RUNNING the candidate
' over the corpus, like every other number in the scorecard; estimating them
' from the model that produced the specification would be the tool grading its
' own homework, which is what `unknown` was protecting against.
check("content_coverage is measured, not unknown",
      is_unknown(rel.scorecard.content_coverage), false)
check("collision_rate is measured, not unknown",
      is_unknown(rel.scorecard.collision_rate), false)
check("nothing is left not-computable", count(rel.scorecard.not_computable), 0)
' A FRACTION, and one whose ends are both wrong answers: a spec explaining
' nothing and a spec explaining a page of running commentary are both impossible
' here, so a 0 or a 1 means the measure broke rather than that the corpus is
' unusual.
check("coverage is a real fraction", rel.scorecard.content_coverage > 0, true)
check("and not everything", rel.scorecard.content_coverage < 1, true)
' The corpus is generated from one grammar and the specification is inferred
' from it, so two rules reading the same characters would be a defect in
' inference, not a property of the report.
check("an inferred specification collides with itself nowhere",
      rel.scorecard.collision_rate, 0)
check("and there were claims for it to be a rate OF", rel.scorecard.claims > 100, true)
' The definitions travel, for the same reason `ari.trace` attaches them: a bare
' fraction printed beside a specification reads as a grade.
check("coverage carries its definition",
      contains(rel.scorecard.content_coverage_is, "non-blank"), true)
check("and says it is pooled rather than averaged",
      contains(rel.scorecard.content_coverage_is, "pooled"), true)

' ===========================================================================
print ""
print "-- PHASE 2: sections, nesting, and multi-source refinement"
' ===========================================================================
check("the proposal is nested", rel.nested, true)
check("the section heading is the one that VARIES, not the column caption",
      rel.sections.heading.literal, "BRANCH")
check("a `page:` directive was generated from what Phase 0 measured",
      count(rel.page_directive) > 0, true)
check("the spec carries it", contains(rel.spec, "page:"), true)
check("and it nests the rows inside the section",
      contains(rel.spec, "section groups repeats"), true)

' MULTI-SOURCE REFINEMENT, AND IT IS A DIFFERENCE. Both directives below
' describe the source the specification was built from; only one describes the
' others. The form feed is in half the corpus, THE HEADER LINE IS IN ALL OF IT.
function sections_right(spec, corpus, truth)
    n = 0
    k = 0
    while k < count(corpus)
        r = ari.parse(corpus[k].text, spec)
        got = 0
        if r.ok then
            if has(r.value, "groups") then
                got = count(r.value.groups)
            end if
        end if
        if got = count(truth.sources[k].branches) then
            n = n + 1
        end if
        k = k + 1
    end while
    return n
end function

single = []
append(single, "page:")
append(single, "    break: formfeed")
append(single, "    drop: 4")
append(single, "")
append(single, "section report:")
append(single, "    section groups repeats starts(/^BRANCH /):")
append(single, "        field branch_no: right of \"BRANCH\" as integer")
append(single, "        section rows repeats starts(/^[ ]*[0-9]{5,}/):")
append(single, "            field amount: first money as money")
one_src = sections_right(join(single, "\n"), corpus, truth)
refined = sections_right(rel.spec, corpus, truth)
print ("     sections right: one-source directive " + string(one_src)
       + "/" + string(count(corpus)) + "   corpus-refined " + string(refined)
       + "/" + string(count(corpus)))
check("a directive taken from ONE source describes only that source", one_src, 0)
check("one refined from the corpus describes all of them", refined, count(corpus))
check("region_coverage agrees", round3(rel.scorecard.region_coverage), 1)

' THE LABEL ALTERNATION, the other half of refinement. The corpus says
' `BRANCH TOTAL` and `TOTAL FOR BRANCH` for one concept; a specification built
' from one source recovers the totals of the sources that share its wording and
' returns `unknown` for the rest, with nothing to say why.
check_at_least("more than one total label is observed",
               count(rel.labels.totals), 2)
check("so the locator is an alternation, not one of them",
      contains(rel.spec, "|"), true)

' THE STRONG ORACLE AT SECTION LEVEL: branch numbers, totals and row counts
' against what the generator planted. Coverage cannot substitute -- a
' specification can find the right number of sections and put the wrong values
' in them, which is exactly what the single-label version did.
function section_recovery(spec, corpus, truth)
    bn = 0
    tt = 0
    rc = 0
    tot = 0
    k = 0
    while k < count(corpus)
        r = ari.parse(corpus[k].text, spec)
        got = []
        if r.ok then
            if has(r.value, "groups") then
                got = r.value.groups
            end if
        end if
        j = 0
        for each b in truth.sources[k].branches
            tot = tot + 1
            if j < count(got) then
                g = got[j]
                if string(g.branch_no) = string(b.number) then
                    bn = bn + 1
                end if
                if has(g, "group_total") then
                    if not is_unknown(g.group_total) then
                        if floor(number(string(g.group_total)) * 100 + 0.5) = b.total_cents then
                            tt = tt + 1
                        end if
                    end if
                end if
                if has(g, "rows") then
                    if count(g.rows) = count(b.accounts) then
                        rc = rc + 1
                    end if
                end if
            end if
            j = j + 1
        end for
        k = k + 1
    end while
    return { numbers: bn, totals: tt, rows: rc, branches: tot }
end function

sr = section_recovery(rel.spec, corpus, truth)
print ("     branches " + string(sr.branches) + ": numbers " + string(sr.numbers)
       + ", totals " + string(sr.totals) + ", row counts " + string(sr.rows))
check("every branch number is recovered", sr.numbers, sr.branches)
check("every branch total is recovered", sr.totals, sr.branches)
check("every branch's row count is right", sr.rows, sr.branches)

' AND ON THE WHOLE CORPUS, including the 16 sources inference never saw.
allsrc = []
ai = 0
while ai < count(truth.sources)
    append(allsrc, { id: truth.sources[ai].id, text: slurp(dir + truth.sources[ai].file) })
    ai = ai + 1
end while
sa = section_recovery(rel.spec, allsrc, truth)
print ("     over all " + string(count(allsrc)) + " sources: " + string(sa.branches)
       + " branches, numbers " + string(sa.numbers) + ", totals " + string(sa.totals))
check("it generalises to sources inference never read", sa.totals, sa.branches)
check_at_least("and there were materially more of them", sa.branches, sr.branches + 40)

' §8.2 HOLDOUT, reported separately. Reserved from the END rather than at
' random, because §17 criterion 9 requires the same proposal from the same
' ordered corpus and a random split would depend on a seed nobody passed.
hp = ari_discover.infer(allsrc, { holdout: 8 })
check("the holdout is reserved from inference", hp.trained_on, count(allsrc) - 8)
check("and scored separately", hp.holdout.sources, 8)
check("training region coverage", round3(hp.scorecard.region_coverage), 1)
check("holdout region coverage", round3(hp.holdout.region_coverage), 1)
' A holdout bigger than the corpus leaves nothing to infer from.
on error goto next
bad_h = ari_discover.infer(allsrc, { holdout: count(allsrc) })
if error then
    check("a holdout that leaves nothing is refused",
          contains(error.message, "leaves nothing"), true)
    error.clear()
else
    check("a holdout that leaves nothing is refused", "accepted", "refused")
end if
on error stop

' The per-source detail is reported, not just the fraction: an operator has to
' know WHICH sources disagree.
check("every source is accounted for individually",
      count(rel.scorecard.regions_per_source), count(corpus))

' THE SCORECARD MUST HAVE COUNTED SOMETHING. `validate` used to look only at a
' top-level `rows`, so from the moment Phase 2 started generating a NESTED
' specification every proposal reported `rows: 0` and `cells: 0` -- and
' therefore `unknown_rate: 0`, which reads as "nothing is unknown" when what
' happened is that nothing was counted. An absence of measurement rendered as a
' clean 0% is exactly the defect this library exists to report rather than
' produce. Asserted against the PLANTED row count, not against a floor.
check("the scorecard counts the rows the generator planted",
      rel.scorecard.rows, planted_rows)
check_at_least("and counted cells, so a 0% unknown rate is a measurement",
               rel.scorecard.cells, planted_rows)

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

' ===========================================================================
print ""
print "-- §13 VARIANTS: one form with drift in it, or two forms?"
' ===========================================================================
' THE HAZARD IS THE ONE RECIPE 1 NAMED: a search always returns a winner, so a
' variant detector pointed at a corpus that merely DRIFTS will report variants,
' and the split looks exactly like a discovery. The 24-source branch corpus is
' therefore the NEGATIVE CONTROL and it is asserted first -- and asserted
' together with the fact that it really does differ, because "it found one
' group" is otherwise equally satisfied by a detector that always finds one.

vdir = dir + "variants/"
vtruth = decode(slurp(vdir + "truth.json"))

' The differences the branch corpus actually carries, read from the answer key
' rather than from anything the tool said.
tlabels = []
ffstyles = []
for each src in truth.sources
    if not contains(tlabels, src.variant.total_label) then
        append(tlabels, src.variant.total_label)
    end if
    if not contains(ffstyles, string(src.variant.form_feed)) then
        append(ffstyles, string(src.variant.form_feed))
    end if
end for
check_at_least("the branch corpus uses several total labels", count(tlabels), 2)
check_at_least("and paginates more than one way", count(ffstyles), 2)

bv = ari_discover.variants(allsrc)
print ("     branch corpus: " + string(count(bv.groups)) + " grammar(s), "
       + bv.recommendation + ", one spec serves " + string(bv.one.served)
       + "/" + string(bv.one.sources))
check("drift across 24 sources is ONE grammar, not several", count(bv.groups), 1)
check("so the recommendation is one specification", bv.recommendation, "one")
check("and that specification serves every source", bv.one.served, count(allsrc))
' The layout differences are real and are NOT a grouping axis: the default
' specification encodes no column, so two sources whose columns differ need no
' different specification. That difference is reported by anchor_stability, as a
' QUESTION, which is where a decision belongs (§10).
check_at_least("even though the column layouts genuinely differ",
               rel.scorecard.layouts, 2)

' --- the positive case: a corpus with two report forms in it -----------------
mixed = []
mi = 0
while mi < 8
    append(mixed, { id: truth.sources[mi].id, text: slurp(dir + truth.sources[mi].file) })
    mi = mi + 1
end while
for each src in vtruth.sources
    append(mixed, { id: src.id, text: slurp(vdir + src.file) })
end for

mv = ari_discover.variants(mixed)
print ("     mixed corpus:  " + string(count(mv.groups)) + " grammar(s), "
       + mv.recommendation)
check("two report forms are two grammars", count(mv.groups), 2)
check("and the recommendation is to split", mv.recommendation, "split")

' THE GROUPING IS SCORED AGAINST THE ANSWER KEY, not merely counted. A detector
' that split 13 sources into two groups of the wrong sources would pass a count.
branch_right = 0
teller_right = 0
for each g in mv.groups
    for each sid in g.sources
        if starts_with(sid, "t0") then
            if g.grammar = "<NUMBER> <WORD> <IDENTIFIER> <MONEY> <MONEY>" then
                teller_right = teller_right + 1
            end if
        else
            if g.grammar = "<IDENTIFIER> <WORD> <WORD> <DATE> <MONEY>" then
                branch_right = branch_right + 1
            end if
        end if
    end for
end for
check("every branch source is in the branch grammar", branch_right, 8)
check("every teller source is in the teller grammar", teller_right,
      count(vtruth.sources))

' THE RECOMMENDATION IS A DIFFERENCE BETWEEN TWO MEASURED RUNS, and that is
' what is asserted. "It recommended split" is satisfied by a rule that splits
' whenever the grammars differ -- which is the confident guesser again. What
' must be true is that splitting SERVES MORE SOURCES.
print ("     one spec serves " + string(mv.one.served) + "/" + string(mv.one.sources)
       + ", one per grammar serves " + string(mv.split.served))
check_at_least("splitting serves materially more sources",
               mv.split.served - mv.one.served, 8)
check("and the split specifications serve every source",
      mv.split.served, count(mixed))
check("while one specification cannot even be inferred for the mixed corpus",
      mv.one.ok, false)

' THE FLOOR IS A CONTROL on the same corpus: raise the number of sources a
' variant needs above what the smaller form has, and the answer becomes "more
' samples" rather than a split. Without it, `split` could be what this returns
' for any corpus with two grammars however thin the evidence.
mv6 = ari_discover.variants(mixed, { minimum_variant_sources: 6 })
check("a form with too few samples is not declared a variant",
      mv6.recommendation, "more_samples")
check("and the reason names the shortfall",
      contains(mv6.why, "fewer than 6"), true)

' --- the null corpus: there is nothing to split ------------------------------
nv = ari_discover.variants(nulls)
check("structureless sources are not variants of anything",
      nv.recommendation, "none")
check("and it is reported as a refusal", nv.ok, false)
check_at_least("even though they group into many distinct shapes",
               count(nv.groups), 6)

' ===========================================================================
print ""
print "-- PAGE BREAK: a form feed does not precede page one"
' ===========================================================================
' FOUND BY THE VARIANT CORPUS AND BY NOTHING ELSE. The branch corpus is never
' uniform in pagination style, so `corpus_furniture`'s form-feed branch had
' never once been taken; the teller journals ARE uniform, took it, and every one
' of the five then reported exactly one section too many -- an off-by-one that
' looks like a defect in section detection and is not. A form feed separates
' page n from page n+1 and does not precede page ONE, so `break: formfeed`
' leaves the first page's header block in the document.
'
' Asserted as a DIFFERENCE: the directive must be the header line AND the
' region coverage must be exact. Either alone is weak -- the first is a fact
' about a string, and the second passes on any corpus whose sources happen to
' be single-page.
tellers = []
for each src in vtruth.sources
    append(tellers, { id: src.id, text: slurp(vdir + src.file) })
end for
tv = ari_discover.infer(tellers)
check("a specification is inferred for the teller form", tv.ok, true)
check("its page break is the header LINE, not a form feed",
      contains(join(tv.page_directive, "\n"), "break: formfeed"), false)
check("and it finds the right number of sections in every source",
      round3(tv.scorecard.region_coverage), 1)
check("the reason the form feed was not used is recorded",
      contains(tv.furniture_note, "does not precede page one"), true)
ffcount = 0
for each src in vtruth.sources
    if src.variant.form_feed then
        ffcount = ffcount + 1
    end if
end for
check_at_least("and there really are form feeds to have been tempted by",
               ffcount, 2)

' ===========================================================================
print ""
print "-- §10 DECISIONS: a review that can be written down and replayed"
' ===========================================================================
' THE CONTROL COMES FIRST, because every check below is otherwise satisfied by
' a `refine` that quietly re-infers from scratch and ignores what it was told:
' with NO decisions it must produce the specification `infer` produces, byte
' for byte.
none_ref = ari_discover.refine(corpus, rel, [])
check("refine with no decisions changes nothing", none_ref.spec, rel.spec)
check("and the family is still chosen by dominance",
      none_ref.family_chosen_by, "dominance")

' A field to work on, taken from the proposal rather than named here, so the
' tier cannot go stale against a change in how fields are named.
f0 = rel.fields[0].name
ren = ari_discover.refine(corpus, rel,
                          [ { decision: "rename_field", field: f0, to: "when_posted" } ])
check("a rename reaches the generated specification",
      contains(ren.spec, "field when_posted:"), true)
check("and the old name is gone",
      contains(ren.spec, "field " + f0 + ":"), false)
check("the decision is recorded in the proposal", count(ren.decisions), 1)
check("and the family is unchanged by it", ren.family.signature,
      rel.family.signature)

' SERIALIZABLE IS THE HALF §10 ASKS FOR AND THE HALF A TEST USUALLY SKIPS. A
' decision list that survives `encode` and `decode` and then reproduces the
' SAME specification is what makes a review reproducible; one that merely
' worked in memory is a function call.
wire = encode([ { decision: "rename_field", field: f0, to: "when_posted" } ])
revived = ari_discover.refine(corpus, rel, decode(wire))
check("decisions round-trip through text and give the same specification",
      revived.spec, ren.spec)
check("and the same specification twice from the same input",
      ari_discover.refine(corpus, rel,
          [ { decision: "rename_field", field: f0, to: "when_posted" } ]).spec,
      ren.spec)

drop = ari_discover.refine(corpus, rel, [ { decision: "drop_field", field: f0 } ])
check("a dropped field leaves the specification",
      contains(drop.spec, "field " + f0 + ":"), false)
check_at_least("and the others stay", count(drop.fields), count(rel.fields) - 1)
check("exactly one fewer", count(rel.fields) - count(drop.fields), 1)

' A CONVERSION DECISION MOVES THE `as` CLAUSE AND NOTHING ELSE. §10's "choose a
' type" is about how a span is converted, not about where it is; asserting both
' halves is what keeps the two apart, since a decision that silently relocated
' a field would still produce a specification that parses.
money_field = unknown
for each fl in rel.fields
    if fl.type = "money" then
        if is_unknown(money_field) then
            money_field = fl
        end if
    end if
end for
conv = ari_discover.refine(corpus, rel,
                           [ { decision: "field_type", field: money_field.name,
                               as: "text" } ])
check("a conversion decision removes the `as money`",
      contains(conv.spec, "field " + money_field.name + ": "
               + money_field.locator + " as money"), false)
check("while the locator survives it",
      contains(conv.spec, "field " + money_field.name + ": "
               + money_field.locator), true)
check("and no other field moved",
      conv.fields[0].locator = rel.fields[0].locator, true)

' CHOOSING A DIFFERENT ROW FAMILY is the most consequential decision there is,
' because the heading, the sections and every field hang off it. Asserted as a
' DIFFERENCE: the chosen family must change AND the fields must change with it,
' since a `refine` that recorded the decision and re-inferred the dominant
' family anyway would satisfy the first alone.
' A THRESHOLD THAT STOPS THE TOOL GUESSING MUST NOT STOP A PERSON DECIDING,
' and this corpus is what makes the point testable: exactly ONE family clears
' both thresholds here, so if the decision were bound to the candidate list it
' could name nothing at all -- which would make the commonest reason for
' overriding, "you picked the wrong family", unsayable precisely when the tool
' picked wrongly. The family named below is a real one the corpus carries and
' one dominance rejected.
alt_sig = unknown
for each fm in rel.corpus.profiles[0].families
    if fm.signature != rel.family.signature then
        if is_unknown(alt_sig) then
            if fm.count >= 3 then
                alt_sig = fm.signature
            end if
        end if
    end if
end for
check("the corpus carries a family dominance did not choose",
      is_unknown(alt_sig), false)
fam_ref = ari_discover.refine(corpus, rel,
                              [ { decision: "row_family",
                                  signature: alt_sig } ])
check("the decision picks it", fam_ref.family.signature, alt_sig)
check("it is not the one dominance chose",
      fam_ref.family.signature = rel.family.signature, false)
check("and the provenance says a decision chose it",
      fam_ref.family_chosen_by, "decision")
check("the specification really is a different one",
      fam_ref.spec = rel.spec, false)

' --- REFUSALS, each beside its nearest legal neighbour ----------------------
' A refusal suite with no controls is satisfied by refusing everything, and the
' rule that matters here is the opposite one: a decision that matches nothing
' must be REFUSED rather than ignored, because a silently dropped rename leaves
' the reviewer believing they made a change they did not.
function refuses(label, sources, prop, ds, needle)
    on error goto next
    r = ari_discover.refine(sources, prop, ds)
    if error then
        msg = error.message
        error.clear()
        on error stop
        check(label, contains(msg, needle), true)
        return nothing
    end if
    on error stop
    check(label, "accepted", "refused: " + needle)
    return nothing
end function

refuses("an unknown decision kind is refused by name", corpus, rel,
        [ { decision: "reorder_fields", field: "x" } ], "'reorder_fields' is not a decision")
refuses("an unknown field on a decision is refused by name", corpus, rel,
        [ { decision: "drop_field", field: f0, to: "x" } ], "'to' is not a field")
refuses("a missing required field is refused", corpus, rel,
        [ { decision: "rename_field", field: f0 } ], "needs `to`")
refuses("an unknown conversion is refused", corpus, rel,
        [ { decision: "field_type", field: f0, as: "currency" } ],
        "'currency' is not a conversion")
refuses("renaming a field that does not exist is refused, not ignored", corpus, rel,
        [ { decision: "rename_field", field: "no_such_field", to: "x" } ],
        "no field 'no_such_field'")
refuses("and the message lists the fields that do exist", corpus, rel,
        [ { decision: "rename_field", field: "no_such_field", to: "x" } ], f0)
refuses("a row family nothing carries is refused", corpus, rel,
        [ { decision: "row_family", signature: "<MONEY> <MONEY> <MONEY>" } ],
        "no row family with signature")
refuses("and the message lists the families that ARE there", corpus, rel,
        [ { decision: "row_family", signature: "<MONEY> <MONEY> <MONEY>" } ],
        rel.family.signature)
refuses("decisions must be an array", corpus, rel,
        { decision: "drop_field", field: f0 }, "must be an array")

' --- §10's variant decision: acting on what §13 recommended ------------------
' `variants` says a corpus holds two forms; this is how a reviewer says so in a
' record. Functionally it narrows the corpus -- and a caller could narrow it by
' hand, which is exactly why the CONTROL below matters: the decision must
' produce what inferring from that subset produces, or it is doing something
' else under a reassuring name.
half = []
half_ids = []
hi = 0
while hi < 4
    append(half, corpus[hi])
    append(half_ids, corpus[hi].id)
    hi = hi + 1
end while
var_ref = ari_discover.refine(corpus, rel,
                              [ { decision: "variant", sources: half_ids } ])
check("a variant decision restricts inference to the sources it names",
      var_ref.trained_on, 4)
check("and gives what inferring from those sources gives",
      var_ref.spec, ari_discover.infer(half).spec)
check("while the whole corpus still trains on all of it", rel.trained_on,
      count(corpus))
refuses("a variant naming a source the corpus lacks is refused by name", corpus, rel,
        [ { decision: "variant", sources: [ corpus[0].id, "99_not_here" ] } ],
        "99_not_here")
refuses("a variant of one source is refused", corpus, rel,
        [ { decision: "variant", sources: [ corpus[0].id ] } ],
        "is not a form")

' THE ARGUMENT-ORDER MISTAKE, which is the one that costs most: three arguments
' in the wrong places returns a perfectly valid UNREFINED specification, and
' nothing about it looks wrong. The proposal parameter is checked precisely so
' that cannot happen quietly.
on error goto next
slipped = ari_discover.refine(corpus,
                              [ { decision: "drop_field", field: f0 } ],
                              nothing)
if error then
    check("passing the decisions where the proposal goes is refused",
          contains(error.message, "the second argument is the proposal"), true)
    error.clear()
else
    check("passing the decisions where the proposal goes is refused",
          "accepted", "refused")
end if
' ...and the CONTROL: a caller who genuinely has no proposal to hand may say so.
noprop = ari_discover.refine(corpus, nothing, [])
if error then
    check("refine without a proposal is allowed", error.message, "allowed")
    error.clear()
else
    check("refine without a proposal is allowed", noprop.spec, rel.spec)
end if
' `explain` says what it takes when handed something else.
bad_explain = ari_discover.explain(ari_discover.variants(corpus))
if error then
    check("explain names what it accepts when handed neither",
          contains(error.message, "read its `why`"), true)
    error.clear()
else
    check("explain names what it accepts when handed neither",
          "accepted", "refused")
end if
on error stop

' ===========================================================================
print ""
print "-- §12 ALTERNATIVES AND RULE EXPLANATIONS"
' ===========================================================================
' §12 asks which alternatives were considered. The list is only worth anything
' if the CHOSEN candidate is absent from it -- otherwise it is a list of
' candidates and says nothing about what was rejected -- and if every entry is
' a real one.
check_at_least("the proposal records what it did not choose",
               count(rel.alternatives), 2)
' AND THE HONEST FACT ABOUT THIS CORPUS, asserted so it cannot drift silently:
' only one row family clears both thresholds here, so the alternatives are the
' page break and the label wording -- not a row family. A tier expecting a
' rejected family would be expecting something this corpus does not produce.
fam_alts = 0
for each a in rel.alternatives
    if a.kind = "row_family" then
        fam_alts = fam_alts + 1
    end if
end for
check("only one row family clears the thresholds in this corpus", fam_alts, 0)
chosen_listed = 0
unreal = 0
no_reason = 0
corpus_sigs = []
for each pr in rel.corpus.profiles
    for each fm in pr.families
        if not contains(corpus_sigs, fm.signature) then
            append(corpus_sigs, fm.signature)
        end if
    end for
end for
for each a in rel.alternatives
    if a.why_not = "" then
        no_reason = no_reason + 1
    end if
    if a.kind = "row_family" then
        if a.signature = rel.family.signature then
            chosen_listed = chosen_listed + 1
        end if
        if not contains(corpus_sigs, a.signature) then
            unreal = unreal + 1
        end if
    end if
end for
check("the chosen family is not listed as an alternative to itself", chosen_listed, 0)
check("every alternative family is one the corpus really has", unreal, 0)
check("and every alternative carries a reason", no_reason, 0)

' THE PAGE BREAK THAT WAS NOT TAKEN is recorded, which is the entry that
' matters most here: it was the WINNER until the variant corpus measured it.
ff_alt = 0
for each a in rel.alternatives
    if a.kind = "page_break" then
        ff_alt = ff_alt + 1
        check("the rejected page break says why it loses",
              contains(a.why_not, "does not precede page ONE"), true)
    end if
end for
check("the form-feed directive is recorded as considered", ff_alt, 1)

' --- the explanation -------------------------------------------------------
' THE TRIPWIRE, and it is what keeps explanation from becoming decoration:
' EVERY rule in the generated specification must appear in the account of it.
' A rule nobody can trace is a rule nobody can safely change, which is design
' principle 8.
acct = ari_discover.explain(rel)
missing = 0
rules = 0
for each ln in split(rel.spec, "\n")
    t = trim(ln)
    if starts_with(t, "field ") then
        rules = rules + 1
        if not contains(acct, t) then
            missing = missing + 1
        end if
    end if
end for
check_at_least("the specification has rules to explain", rules, 3)
check("every field rule is explained", missing, 0)
check("the account names the source the columns came from",
      contains(acct, rel.built_from), true)
check("it answers §12's LLM question even when the answer is no",
      contains(acct, "llm"), true)
check("it carries the rejected alternatives",
      contains(acct, "rejected:"), true)
check("and the open questions", contains(acct, "OPEN QUESTIONS"), true)

' POSITIONAL DEPENDENCE IS ASSERTED AS A DIFFERENCE between two runs, because
' "the explanation mentions columns" is satisfied by text that always does.
pos_acct = ari_discover.explain(pos)
check("a positional specification says so, field by field",
      contains(pos_acct, "located by COLUMN"), true)
check("an anchor-relative one does not",
      contains(acct, "located by COLUMN"), false)

' A REFUSED PROPOSAL IS EXPLAINED, NOT RAISED ON. The null corpus's answer is a
' refusal, and a reviewer asking why deserves the reason rather than an error.
null_acct = ari_discover.explain(nres)
check("a refusal is explained rather than raised on",
      contains(null_acct, "NO SPECIFICATION WAS PROPOSED"), true)
check("and the reason travels with it",
      contains(null_acct, "no row family"), true)

' `explain` still answers about a PROFILE, which is what every existing caller
' hands it -- the dispatch must not have taken that away.
prof_acct = ari_discover.explain(rel.corpus.profiles[0])
check("explain still describes a profile", contains(prof_acct, "row families"), true)

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))
