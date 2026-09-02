#!/usr/bin/env bash
set -uo pipefail

# The Business Automation Reasoning DESIGN LABORATORY
# (docs/automation_reasoning_design.md §15: "the cookbook is initially a design
# laboratory"). These are not user documentation and the libraries they argue
# for do not exist yet. They are EXPERIMENTS, written in today's gBASIC, whose
# job is to make a design claim measurable instead of plausible.
#
# Recipe 1 exists to answer one question about §11's automatic decomposition,
# which is the document's most attractive proposal: does a drill-down that
# names "67% of the decline is attributable to Northeast -> Syracuse ->
# Outdoor" actually mean anything?
#
# It runs the SAME decomposition over two populations:
#   RUN A -- a real 45% collapse planted in one known cell.
#   RUN B -- nothing planted at all. Only lognormal noise.
#
# Both decline by 1.8%. Both produce a confident three-level chain. One is a
# real cause and the other is nothing, and the output cannot tell them apart.
# That is the finding, and these assertions are what keep it a finding rather
# than an anecdote somebody remembers wrongly later.
#
# The golden pins the whole transcript. The assertions below pin the four
# things that make the experiment MEAN what the recipe says it means -- each
# would still leave a passing golden if it quietly stopped being true, because
# a golden records whatever came out.

cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER the experiment runs and is deterministic\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/01_sales_decline.bas \
        >"$scratch/out" 2>"$scratch/err"; then
    pass "01_sales_decline runs"
else
    pass_or=; fail "01_sales_decline runs ($(head -1 "$scratch/err"))"
fi
if diff -u examples/automation_lab/01_sales_decline.out "$scratch/out" >/dev/null 2>&1; then
    pass "output matches the committed golden"
else
    fail "output matches the committed golden"
    diff -u examples/automation_lab/01_sales_decline.out "$scratch/out" | head -15
fi

printf 'TIER the finding is still the finding\n'

# 1. The planted run must FIND the planted cell. If it stops doing that, the
#    decomposition is broken and run B proves nothing.
if grep -q "CONCLUSION: Northeast -> Northeast-2 -> Outdoor" "$scratch/out"; then
    pass "run A recovers the planted cell"
else
    fail "run A recovers the planted cell ($(grep -m1 CONCLUSION "$scratch/out"))"
fi

# 2. The null run must reach a DIFFERENT conclusion, confidently. This is the
#    hazard itself: a chain built out of noise.
concl_b=$(grep "CONCLUSION" "$scratch/out" | sed -n 2p)
if [ -n "$concl_b" ] && ! grep -q "Northeast -> Northeast-2 -> Outdoor" <<<"$concl_b"; then
    pass "run B reaches a confident conclusion of its own (${concl_b##*: })"
else
    fail "run B reaches a confident conclusion of its own (got '$concl_b')"
fi

# 3. THE PAIRING IS THE ARGUMENT. Both runs must decline by the same headline
#    percentage. If the null run stops matching, a reader can dismiss it as
#    "well, that one barely moved" and the demonstration is gone.
a_pct=$(grep -m1 "^change" "$scratch/out" | grep -o '(-\?[0-9.]*%)')
b_pct=$(grep "^change" "$scratch/out" | sed -n 2p | grep -o '(-\?[0-9.]*%)')
if [ "$a_pct" = "$b_pct" ] && [ -n "$a_pct" ]; then
    pass "both runs show the same headline decline $a_pct -- real cause and noise are indistinguishable at the top line"
else
    fail "both runs show the same headline decline (A=$a_pct B=$b_pct)"
fi

# 4. And the null model must SEPARATE them, or the recipe diagnoses a problem
#    it cannot answer. The threshold is derived from the search width, not
#    hardcoded, so this also pins that the derivation still runs.
if grep -q "is beyond that. The cell is worth explaining." "$scratch/out" \
   && grep -q "returns a winner whether or not anything happened" "$scratch/out"; then
    pass "the empirical null clears run A and rejects run B"
else
    fail "the empirical null clears run A and rejects run B"
fi

# 5. The threshold must be DERIVED. A constant would silently stop scaling
#    with the search, which is the mistake the recipe is about.
if grep -q "searching 60 cells, the worst of them lands near z = -2.86" "$scratch/out"; then
    pass "the threshold is computed from the search width (60 cells -> 2.86)"
else
    fail "the threshold is computed from the search width"
fi


printf 'TIER recipe 1 through the library (02), and the two must agree\n'
if GBASIC_PATH=stdlib ./gbasic examples/automation_lab/02_explain_change.bas \
        >"$scratch/lib" 2>/dev/null; then
    pass "02_explain_change runs"
else
    fail "02_explain_change runs"
fi
if diff -u examples/automation_lab/02_explain_change.out "$scratch/lib" >/dev/null 2>&1; then
    pass "02 matches its committed golden"
else
    fail "02 matches its committed golden"
    diff -u examples/automation_lab/02_explain_change.out "$scratch/lib" | head -12
fi

# THE CROSS-CHECK. Two independent implementations over identical data: the
# hand-rolled decomposition in 01 has no null model, the library in 02 does.
# They must agree about WHERE the change is concentrated and disagree only
# about whether it MEANS anything. Agreement on the cell is what makes 02 a
# check on the library rather than a restatement of it.
if grep -q "leading cell      Northeast -> Northeast-2 -> Outdoor" "$scratch/lib" \
   && grep -q "CONCLUSION: Northeast -> Northeast-2 -> Outdoor" "$scratch/out"; then
    pass "hand-rolled and library agree on the planted cell"
else
    fail "hand-rolled and library agree on the planted cell"
fi
if grep -q "leading cell      Southeast -> Southeast-2 -> Electronics" "$scratch/lib" \
   && grep -q "CONCLUSION: Southeast -> Southeast-2 -> Electronics" "$scratch/out"; then
    pass "and on the noise run's leader"
else
    fail "and on the noise run's leader"
fi

# And where they differ is the whole point of the library.
a_verdict=$(grep -c "beyond ordinary variation" "$scratch/lib")
b_verdict=$(grep -c "within ordinary variation" "$scratch/lib")
if [ "$a_verdict" = 1 ] && [ "$b_verdict" = 1 ]; then
    pass "the library separates them: one verdict each way"
else
    fail "the library separates them (beyond=$a_verdict within=$b_verdict)"
fi

# THE RESULT THAT SURPRISED ME MOST, pinned so it cannot quietly change: BOTH
# runs withhold shares, because the -1.8% headline is itself not
# distinguishable from zero. "82.6% of the decline" in recipe 1 was a share of
# a decline that had never been established -- even in the run where one cell
# really did collapse.
if [ "$(grep -c 'shares reportable false' "$scratch/lib")" = 2 ]; then
    pass "both runs withhold shares -- the aggregate decline was never established"
else
    fail "both runs withhold shares"
fi

printf '\nrun_automation_lab: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
