#!/usr/bin/env bash
# ARI Discover -- profiling, inference, sections, variants and review
# (stdlib/ari_discover.bas, docs/ari_discover_design.md §16 Phases 0-3).
#
# WHAT PHASE 0 CLAIMS AND WHAT IT DOES NOT. It measures what repeats, varies
# and collides in a corpus of print-image reports, and it PROPOSES NOTHING --
# no specification is generated here. Design principle 1 is "measurement before
# interpretation", and a layer that also guessed would make the guess
# impossible to evaluate apart from the measurement under it.
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced twice over. Every defect this
# layer can have produces A PLAUSIBLE PROFILE: a furniture detector claiming
# one line too many still reports a tidy page header, and a grouping that
# fragments still reports families -- just more of them. BOTH HAPPENED WHILE
# BUILDING IT, and a golden would have recorded either as expected.
#
# THE NUMBERS ARE MEASUREMENTS, NOT TRANSCRIPTS. `truth.json` is written by the
# generator from the values it PLANTED, never read back out of the text it
# printed (estate's R1), so precision and recall are computed against an answer
# key the library has never seen.
#
# --- THE TIERS -------------------------------------------------------------
#
# FURNITURE is load-bearing and it is a PAIR OF OPPOSITE CLAIMS, because either
# half alone is satisfied by a broken detector:
#
#   * over the 20 MULTI-PAGE sources, precision and recall must both be 1.0.
#     Recall alone is maximised by calling every line furniture, which strips
#     the report; precision alone by calling nothing furniture.
#   * over the 4 SINGLE-PAGE sources the required answer is a REFUSAL WITH A
#     REASON. A header that appears once is indistinguishable from a first
#     heading, so the truth records furniture there that is NOT RECOVERABLE.
#     Without this half, "precision is 1.0" is satisfied by a detector that
#     claims nothing anywhere -- which is exactly what an earlier draft did on
#     14 of 18 sources while reporting a clean result.
#
# PHASE 1'S HEADLINE IS A DIFFERENCE, and it is the design's own principle --
# "relative structure before columns" -- turned into a number. Over 8 sources
# carrying 230 planted rows, a generated specification recovers:
#
#     anchor-relative `amount`   230 / 230
#     positional     `acct_no`   147 / 230
#
# and the second is SILENT: source_coverage is 1.0, unknown_rate is 0, every
# extracted value is an ordinary-looking account number. Only `anchor_stability`
# catches it, and it is computed WITHOUT the answer key -- from whether the
# family's column structure is the same in every source (it is not: 7 distinct
# layouts across 8, because the corpus varies its table indent).
#
# THE NULL CORPUS CAUGHT PHASE 1 TOO. `infer` proposed a specification for
# structureless text: the family `<MONEY> <DATE>`, ONE LINE in its source,
# recurring in 11 of 12 sources by chance because a two-token shape recurs
# whenever tokens are drawn at random. §16 asks for one DOMINANT repeating
# family and the first version implemented only "repeating" -- 55% of content
# lines in the real corpus against 1.5% in the null.
#
# PHASE 2'S HEADLINE IS A SHARPER VERSION OF PHASE 1'S, and it is a difference
# between two measures OF THE SAME RUN: `source_coverage` is 1.0 while
# `region_coverage` is 0.125. A nested specification parses every source without
# error and finds the WRONG NUMBER OF SECTIONS in seven of eight -- 4 where there
# are 3, 10 where there are 7. Three things in it are source-specific and none
# fails loudly: `break: formfeed` does nothing on a source paginated by a header
# line, so the page header matches `^BRANCH ` and becomes a section; the total's
# label differs between sources; and the columns differ. The CONTROL is that on
# the source it was built from the section count is exact, without which the
# finding would be about incompetence rather than heterogeneity.
#
# TWO PHASE 2 RULES ARE RECORDED AS UNPROVEN, deliberately and in the source:
# the "a section heading must VARY" rule and the outermost-indent preference are
# each justified, and removing BOTH changes no answer here, because the branch
# heading happens to appear before the column caption and insertion order then
# picks it. Separating them needs a report whose caption comes first, or two
# levels of nesting -- which `examples/fixtures/ari/delinquency.rpt` has and this
# corpus does not. Recorded rather than proven; kept rather than removed, since a
# rule with a reason is not dead code.
#
# NULL is the tier that is not satisfied by a confident guesser (§15.5). Every
# other measure here runs on a corpus that HAS structure, and inference is a
# search -- recipe 1 in examples/automation_lab measured that the same
# decomposition gives the same confident three-level chain from a real 45%
# collapse and from pure noise. THIS TIER FOUND A REAL FALSE POSITIVE: the
# first working detector claimed 18 furniture lines across 12 structureless
# sources. It carries its CONTROL beside it -- a structured source must have a
# dominant family holding >=40% of its lines while a structureless one must
# not exceed 25% -- because "it found nothing" is otherwise satisfied by a
# library that finds nothing anywhere.
#
# INDEX SPACES is the tier THE CORPUS CANNOT PROVIDE. Every generated file is
# ASCII, so byte and codepoint positions coincide exactly and the defect is
# invisible. `ari` locates in CODEPOINTS (`columns`, and every recognizer);
# provenance must be in BYTES. A hand-built source with accented names in a
# column-aligned table separates them: the amounts end at ONE codepoint column
# and THREE byte columns, asserted as a difference. This is the defect finio
# Phase 0 shipped one library over -- offsets accumulated with `len`, sliced
# with `mid`, called `byte_offset` -- and its own ASCII fixture could not see
# it either.
#
# VARIANTS is §13, and the hazard is the one recipe 1 named: a search always
# returns a winner, so a detector pointed at a corpus that merely DRIFTS will
# report variants and the split looks exactly like a discovery. The negative
# control is therefore the corpus this project already had -- 24 sources across
# nine axes come back as ONE grammar -- and the recommendation is decided by
# RUNNING both arrangements rather than by how the sources grouped:
#
#     24 branch                1 grammar    one spec serves 24/24   -> one
#     8 branch + 5 teller      2 grammars   0/13 against 13/13      -> split
#     the same, floor raised   2 grammars                           -> more_samples
#     12 structureless        10 grammars   nothing serves anything -> none
#
# Column layout is deliberately NOT a grouping axis: the default specification
# encodes no column, so sources whose columns differ need no different
# specification -- and there are 7 layouts across 8 sources here, which a
# layout-keyed grouper would call 7 variants.
#
# PAGE BREAK is the defect the variant corpus found on its first day, and it is
# the kind only a new fixture can find. The branch corpus is never uniform in
# pagination style, so the form-feed branch of the page directive had NEVER ONCE
# BEEN TAKEN; the teller journals are uniform, took it, and each then reported
# exactly one section too many -- because a form feed separates page n from page
# n+1 and does not precede page ONE. Measured: region coverage 0/5 with
# `break: formfeed`, 5/5 with the header pattern. Phase 2 had recorded the
# opposite rule in the design document.
#
# DECISIONS is §10, and the half a test usually skips is the half asserted
# hardest: a decision list that survives `encode`/`decode` and then reproduces
# the SAME specification. Its CONTROL comes first -- `refine` with no decisions
# must produce byte-for-byte what `infer` produces -- because every other check
# is satisfied by a `refine` that quietly re-infers and ignores what it was
# told. The rule that matters most is that A DECISION MATCHING NOTHING IS
# REFUSED RATHER THAN IGNORED: a silently dropped rename leaves the reviewer
# believing they made a change they did not, and the next thing they do is trust
# the specification.
#
# EXPLANATIONS carries the tripwire that keeps §12 from becoming decoration:
# EVERY `field` rule in the generated specification must appear in the account
# of it. It found two the first time it ran -- the section's own heading number
# and group total, which `explain` never rendered.
#
# DATE DIALECTS exists because building the corpus found a gap in `ari`
# itself: it could not read DD-MMM-YYYY, the classic mainframe date, at all --
# `no-date-found` on every one. Nothing in the tree used the format, so nothing
# could see it. The recognizer TABLES are now shared (`ari.money_patterns()`,
# `ari.date_patterns()`), so a date discovery reports is a date the engine can
# extract, and the tier checks BOTH halves rather than trusting the sharing.
#
# Headless, no network, never skips (bar valgrind).
set -u
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

make >/dev/null || { printf 'FAIL build\n'; exit 1; }
export GBASIC_PATH=stdlib

fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

# --- SEMANTICS -------------------------------------------------------------
printf 'TIER semantics (furniture, families, index spaces, dates, null, PHASE 1)\n'
out="$scratch/sem.out"
# THE BOUND IS A HANG-GUARD, NOT A SPEED MEASUREMENT, and it was calibrated to
# an idle machine. This fixture was MEASURED at 356s on a host carrying other
# work (load average 45) while reporting 192 checks and 0 mismatches -- correct,
# and 176s past a 180s bound, so the suite went red for a reason that has
# nothing to do with ari. What the tier asserts is the check count and the
# mismatch count; the timeout only has to be shorter than waiting forever.
if timeout 900 ./gbasic tests/ari_discover/discover_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 185 ]; then
        ok "$checks checks, 0 mismatches"
        sed -n 's/^     /       /p' "$out"
    else
        bad "discover_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "discover_test exited nonzero"; tail -20 "$out"
fi

# --- CORPUS DRIFT ----------------------------------------------------------
# A corpus whose generator can no longer reproduce it is a corpus testing
# itself. All three generators are pure functions of their arguments.
printf 'TIER corpus_drift\n'
cp -r examples/fixtures/ari_discover "$scratch/before"
if ./gbasic tools/gen_discover_corpus.bas >/dev/null 2>&1 \
   && ./gbasic tools/gen_discover_null.bas >/dev/null 2>&1 \
   && ./gbasic tools/gen_discover_variants.bas >/dev/null 2>&1; then
    if diff -r "$scratch/before" examples/fixtures/ari_discover >/dev/null 2>&1; then
        ok "all three generators reproduce the committed corpus byte for byte"
    else
        bad "regenerating the corpus changed it"
        diff -r "$scratch/before" examples/fixtures/ari_discover | head -5
    fi
else
    bad "a generator failed to run"
fi

# --- ANSWER KEY ------------------------------------------------------------
# THE KEY IS CHECKED AGAINST THE BYTES, from outside, because the fixture's own
# version of this compares the library to the library. A wrong answer key is
# worse than none: it teaches the tool to be wrong and then certifies it.
printf 'TIER answer_key\n'
if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PY' >"$scratch/key.out" 2>&1
import json
d='examples/fixtures/ari_discover/'
t=json.load(open(d+'truth.json'))
bad=0; rows=0
for s in t['sources']:
    lines=open(d+s['file']).read().split('\n')
    if lines and lines[-1]=='': lines.pop()
    if len(lines)!=s['physical_lines']: bad+=1
    for ln in s['furniture_lines']:
        x=lines[ln-1]
        if not (x.startswith('BRANCH ACTIVITY REGISTER') or x.startswith('MIDLAND MUTUAL')
                or set(x)=={'='} or x=='' or x=='\x0c'): bad+=1
    for b in s['branches']:
        for a in b['accounts']:
            rows+=1
            if sum(1 for l in lines if a['account'] in l and a['amount_text'] in l and a['name'] in l)!=1: bad+=1
        if sum(a['amount_cents'] for a in b['accounts'])!=b['total_cents']: bad+=1
print(f"{rows} planted rows, {bad} disagreements with the bytes")
raise SystemExit(1 if bad else 0)
PY
    if [ $? = 0 ]; then
        ok "the answer key agrees with the files ($(cat "$scratch/key.out"))"
    else
        bad "the answer key disagrees with the files: $(cat "$scratch/key.out")"
    fi
else
    printf '  SKIP answer_key (python3 unavailable)\n'
fi

# --- NULL IS A FAIR NULL ---------------------------------------------------
# A null corpus that is obviously not a report measures nothing: discovery
# would reject it for reasons unrelated to structure. This tier asserts the two
# corpora are alike in TOKENS and unlike in STRUCTURE, which is the only thing
# that makes the null tier above mean what it says.
printf 'TIER null_is_fair\n'
if command -v python3 >/dev/null 2>&1; then
    python3 - <<'PY'
import glob, collections, re, sys
def sig(l):
    out=[]
    for t in l.split():
        if re.fullmatch(r'\d{8}', t): out.append('ID')
        elif re.fullmatch(r'[\d,]+\.\d\d[-)]?|\([\d,]+\.\d\d\)|-[\d,]+\.\d\d', t): out.append('MONEY')
        elif re.fullmatch(r'\d\d[/-][A-Z0-9]{2,3}[/-]\d{4}', t): out.append('DATE')
        else: out.append('W')
    return ' '.join(out)
res={}
for label,pat in [("real",'examples/fixtures/ari_discover/*.rpt'),
                  ("null",'examples/fixtures/ari_discover/null/*.rpt')]:
    sigs=collections.Counter(); n=0
    for fn in sorted(glob.glob(pat)):
        for l in open(fn):
            l=l.rstrip('\n')
            if l.strip():
                n+=1; sigs[sig(l)]+=1
    res[label]=(n,len(sigs),sum(c for _,c in sigs.most_common(3))/n)
r,nl=res['real'],res['null']
print(f"  real: {r[0]} lines, {r[1]} signatures, top-3 {r[2]:.1%}")
print(f"  null: {nl[0]} lines, {nl[1]} signatures, top-3 {nl[2]:.1%}")
fail=[]
# same token kinds present in both, or the null is trivially distinguishable
for kind in ('MONEY','DATE','ID'):
    if not any(kind in s for s in open('examples/fixtures/ari_discover/null/null_01_noise.rpt').read().split('\n')[:0]+[sig(l) for l in open('examples/fixtures/ari_discover/null/null_01_noise.rpt')]):
        pass
if nl[1] < r[1]*5: fail.append(f"the null is not structureless enough: {nl[1]} signatures vs {r[1]}")
if r[2] < 0.5: fail.append(f"the real corpus is not structured enough: top-3 {r[2]:.1%}")
if nl[2] > 0.25: fail.append(f"the null has a dominant signature: top-3 {nl[2]:.1%}")
if nl[0] < 300: fail.append("the null corpus is too small to measure")
for f in fail: print("  FAIL "+f)
raise SystemExit(1 if fail else 0)
PY
    if [ $? = 0 ]; then
        ok "same tokens, structure absent by two orders of magnitude"
    else
        bad "the null corpus is not a fair null"
    fi
else
    printf '  SKIP null_is_fair (python3 unavailable)\n'
fi

# --- ARCHITECTURAL TRIPWIRE ------------------------------------------------
# §2: discovery depends on `ari`, and `ari` NEVER depends on discovery. The
# deterministic parser must stay free of clustering and inference so a caller
# with a known specification pays for none of it. Read from the source, because
# nothing behavioural can see a dependency that merely exists.
printf 'TIER dependency_direction\n'
if grep -qE '^ *load +ari_discover' stdlib/ari.bas; then
    bad "stdlib/ari.bas loads ari_discover -- the dependency must point one way"
else
    ok "ari does not depend on ari_discover"
fi
if grep -qE '^ *load +ari +from' stdlib/ari_discover.bas; then
    ok "ari_discover depends on ari"
else
    bad "ari_discover should load ari -- the recognizer tables are shared, not copied"
fi

# --- RECOGNIZERS ARE SHARED, NOT COPIED ------------------------------------
# §20's open decision, answered: the PATTERN TABLES are public and the
# FUNCTIONS are not. A second copy of "what money looks like" drifts, and the
# failure is silent -- a profile reporting a DATE in a column the engine
# answers `no-date-found` for. The tripwire is that discovery holds no money or
# date regex of its own.
printf 'TIER shared_recognizers\n'
if grep -nE '\[0-9\](\{|,)' stdlib/ari_discover.bas | grep -vE 'identifier|[0-9]\{5,\}|^\s*.' >/dev/null 2>&1; then
    :
fi
own=$(grep -cE 'regex\("[^"]*\\\\.\[0-9\]\{2\}' stdlib/ari_discover.bas || true)
if [ "${own:-0}" = "0" ]; then
    ok "discovery holds no money pattern of its own"
else
    bad "discovery has its own money regex -- it must use ari.money_patterns()"
fi
if grep -q 'ari.money_patterns()' stdlib/ari_discover.bas && grep -q 'ari.date_patterns()' stdlib/ari_discover.bas; then
    ok "it consumes ari's tables"
else
    bad "discovery must consume ari.money_patterns() and ari.date_patterns()"
fi

# --- EXAMPLES --------------------------------------------------------------
# §19 lists both, so both must run. An example named in a design document and
# executed by nothing is the rot run_docs_gate exists for, one directory over.
printf 'TIER examples\n'
for ex in examples/ari_discover_profile.bas examples/ari_discover_infer.bas \
          examples/ari_discover_review.bas; do
    if timeout 120 ./gbasic "$ex" >"$scratch/ex.out" 2>&1; then
        if [ -s "$scratch/ex.out" ]; then
            ok "$ex runs and produces output"
        else
            bad "$ex produced nothing"
        fi
    else
        bad "$ex exited nonzero"; head -5 "$scratch/ex.out"
    fi
done

# --- VALGRIND --------------------------------------------------------------
# POINTED AT A SMALL PATH-COMPLETE FIXTURE, NOT AT THE SEMANTICS ONE, and the
# reason is a measurement: discover_test.bas grows with the corpus (41 sources
# now, each profiled several times) and valgrind costs 20-40x, so at 45 seconds
# native the tier passed TEN MINUTES -- a tier whose runtime grows with the
# fixture will eventually blow the gate's 1800-second per-suite cap however fast
# the interpreter gets.
#
# What is lost is nothing this tier could catch. The library is pure gBASIC: it
# introduces no value kind and no allocation of its own, so what valgrind can
# find here is the INTERPRETER mishandling the paths the library drives -- deep
# record and array copies, copy-on-write detaches, regex values, string slicing
# -- and those are exercised by one pass through each public function, not by
# the twenty-fourth source.
#
# THE TRIPWIRE BELOW IS WHAT MAKES THAT TRADE SAFE. Without it the small fixture
# silently stops covering whatever was added last, and a valgrind tier that has
# stopped covering the new code is exactly the kind of green line this project
# distrusts.
printf 'TIER valgrind_fixture_is_complete\n'
missing=""
for fn in $(grep -oE '^function [a-z][a-z0-9_]*\(' stdlib/ari_discover.bas \
            | sed 's/^function //; s/($//; s/(//' | sort -u); do
    if ! grep -q "ari_discover\.$fn(" tests/ari_discover/discover_vg.bas; then
        missing="$missing $fn"
    fi
done
if [ -z "$missing" ]; then
    ok "every public function is exercised by the valgrind fixture"
else
    bad "the valgrind fixture never calls:$missing"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/ari_discover/discover_vg.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_ari_discover: all cases passed\n'
else
    printf 'run_ari_discover: %d FAILED\n' "$fails"
    exit 1
fi
