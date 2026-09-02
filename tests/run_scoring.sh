#!/usr/bin/env bash
set -uo pipefail

# credit scorecards (docs/scoring_design.md) -- the first of the two items
# docs/credit_analytics_design.md §9 deferred until the measurement layer and
# the population existed. Both now do.
#
# `credit` measures what a book has already DONE. This turns a population into
# a model that RANKS risk, and then into the artefact a credit committee
# approves: a table of attributes and points. Deliberately the credit-specific
# half only -- `stats.logistic_regression` is the engine and is not
# reimplemented here.
#
# SELF-CHECKING, NOT GOLDEN, AND FORCED. Every defect this library can produce
# is a plausible number:
#
#   * a WOE orientation that flips every sign, yielding a scorecard with the
#     SAME discrimination pointed at the people who will not pay;
#   * an AUC below 0.5 quietly turned the right way up, converting the most
#     consequential error in the field into a mediocre-looking result that
#     gets deployed;
#   * a point scale running backwards.
#
# A golden would record all three as expected and then defend them.
#
# THE EXPECTED FIGURES COME FROM OUTSIDE gBASIC. WOE, IV, AUC, Gini, KS, the
# scaling constants and PSI were all computed in Python from the bin counts,
# and they are evidence only because of that -- a fixture asserting what the
# library said is a transcript, not a check.
#
# TWO TIERS DO THE LOAD-BEARING WORK, and neither is a value comparison.
# ORIENTATION IS A DIFFERENCE: the two conventions must give WOE of equal
# magnitude and opposite sign, which is the only shape that can tell a library
# honouring the declaration from one ignoring it -- every value check in the
# file passes on a library that hardcodes one convention. And DIRECTION runs
# the whole pipeline end to end: bin, fit a real logistic regression on the
# WOE column, scale to points, and require the KNOWN BADS to score BELOW the
# known goods by a material margin. Every other tier checks a component; that
# one checks the components are wired together the right way up, and no sign
# error anywhere above survives it.
#
# The AUC check is deliberately made twice over: once against the external
# value, and once against a BRUTE-FORCE PAIR COUNT written in the fixture
# itself. The library uses the rank identity, so that is a second
# implementation rather than the same function called twice -- the PLAT-NUMFMT
# lesson about oracles that read our own output.
#
# Headless, GI-independent, never skips (bar valgrind): pure gBASIC over
# `stats`.

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0; failures=0
pass() { checks=$((checks+1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks+1)); failures=$((failures+1)); printf '  FAIL %s\n' "$1"; }

printf 'TIER semantics\n'
if GBASIC_PATH=stdlib ./gbasic tests/scoring_test.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "scoring_test exits 0"
else
    fail "scoring_test exits 0 ($(head -1 "$scratch/err"))"
fi
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "no mismatches"
else
    fail "no mismatches"
    grep "^MISMATCH" "$scratch/out" | head -10
fi

# A COVERAGE FLOOR. A self-checking fixture that stops running its checks
# reports "mismatches: 0" and passes, which is the way this style of suite
# goes quietly vacuous.
n=$(sed -n 's/^checks: //p' "$scratch/out")
if [ -n "$n" ] && [ "$n" -ge 50 ]; then
    pass "check count floor ($n checks)"
else
    fail "check count floor (got '${n:-none}', want >= 50)"
fi

# And the named tiers must actually have run. Without this the floor above is
# satisfied by fifty checks of anything.
printf 'TIER the load-bearing tiers ran\n'
for needle in \
    "every bin's woe flips sign under the other orientation" \
    "the known bads score BELOW the known goods" \
    "a reversed model reports its auc BELOW 0.5, not flipped" \
    "auc against a brute-force pair count" \
    "the SAME empty bin is accepted once a smoothing is declared"
do
    if grep -qF "ok   $needle" "$scratch/out"; then
        pass "ran: $needle"
    else
        fail "ran: $needle"
    fi
done

printf 'TIER stderr is clean\n'
if [ ! -s "$scratch/err" ]; then
    pass "no warnings or diagnostics on stderr"
else
    fail "no warnings or diagnostics on stderr"
    head -3 "$scratch/err"
fi

# --- valgrind ----------------------------------------------------------------
# Small on purpose: the point is the allocation paths, not the population.
printf 'TIER valgrind\n'
if vg_available; then
    cat >"$scratch/vg.bas" <<'EOF'
load scoring
xs = []
ys = []
for i = 1 to 30
    append(xs, i)
    append(ys, 0)
next
for i = 1 to 20
    append(xs, i)
    append(ys, 1)
next
bins = scoring.bin_numeric(xs, ys, [10, 20])
t = scoring.woe_table(bins, { orientation: "good_bad" })
u = scoring.woe_table(bins, { orientation: "bad_good", smoothing: 0.5 })
a = scoring.auc(xs, ys)
k = scoring.ks(xs, ys)
sc = scoring.scaling({ base_score: 650, base_odds: 20, pdo: 20 })
p = scoring.psi([10, 20, 30], [12, 18, 33])
print string(count(t.rows)) + " " + string(a.reversed) + " " + string(p.note)
print string(round(scoring.points_of(sc, scoring.log_odds_of(sc, 700)), 6))
on error goto next
x = scoring.woe_table(bins, { orientation: "nope" })
print error.message
error.clear()
x = scoring.woe_of(t, "absent")
print error.message
error.clear()
on error stop
EOF
    if GBASIC_PATH=stdlib vg_run ./gbasic "$scratch/vg.bas" \
            >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_scoring: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
