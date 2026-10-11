#!/usr/bin/env bash
# `rules` -- typed business rules as DATA (stdlib/rules.bas,
# docs/gbasic-rules-library-proposal.md). FIRST INCREMENT, and it is a VERTICAL
# SLICE rather than a layer: the proposal's sequence puts "specify record
# schemas" first and the agreement property fifth, and both are here, because a
# schema chosen before a real rule has been encoded through it is a guess and a
# condensation whose agreement with its source is checked two increments later
# is a condensation nobody can still change.
#
# THE SUBJECT IS A DECISION, WHICH IS WHY THIS IS SELF-CHECKING AND NOT GOLDEN,
# and here that is forced rather than preferred: every defect this library can
# have produces A PLAUSIBLE DECISION -- a payment capped at the wrong figure, a
# condition that quietly became false, an "unresolved" where the rule was
# actually wrong. A golden would record the wrong payment as the expected output
# and defend it, which is exactly how a decision engine comes to be trusted
# while being wrong.
#
# THE LOAD-BEARING TIER IS AGREEMENT, over GENERATED stacks. Condensation is
# only safe if it changes nothing, and that is the one claim the whole design
# rests on; it is also the only check that fails when a fold is SUBTLY wrong
# rather than obviously so. The two paths are genuinely different code --
# `evaluate` walks the rules FORWARD and evaluates every condition whether or
# not a later rule overwrites the result, `evaluate_compiled` walks nested
# conditionals LATEST-FIRST and short-circuits -- so they visit conditions in
# opposite orders and the compiled one visits fewer. Agreement is therefore
# asserted on the TARGET RESULTS and deliberately NOT on the node counters or
# the trace: the two are MEANT to do different amounts of work.
#
# AND THE CENSUS IS A TIER, not a note, because the first version of that
# generator agreed perfectly while exercising nothing. Measured: 200 stacks gave
# 9 conditions that fired against 184 that did not, and 69% of all results were
# errors -- so the property was being checked almost entirely on stacks where no
# override ever applied, which is the one case that needs no ordering to get
# right. Conditions are drawn from a boolean-shaped generator now, and the
# census floors are what stop that regressing.
#
# THE DEPTH CAP IS MEASURED AGAINST THE CRASH, which is the tier nobody would
# think to write from the proposal. The proposal cited `try_decode`'s 10,000
# level cap; that is a cap on a C walk, and THIS walk is gBASIC, whose own
# recursion segfaults with NO DIAGNOSTIC AND NO OUTPUT -- measured between 380
# and 400 levels for this evaluator on an 8 MB stack, consistent with DOGFOOD's
# 936/937 for a bare tail call. A cap of 10,000 would be a cap that never fires
# in front of a crash that always does, so the tier asserts the MARGIN: lift the
# cap and the interpreter must still survive twice the shipped cap. It goes red
# if anybody raises `max_depth` toward the crash, or if a frame gets dearer.
#
# Headless, no network, never skips (bar valgrind).
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# ---------------------------------------------------------------------------
printf 'TIER the real contract, the logic table, the boundary, the hostile stack\n'
if ! timeout -k 5 180 ./gbasic tests/rules_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out" | head -10; fail "a decision came out wrong"
elif [ -s "$work/err" ]; then
    cat "$work/err"; fail "the fixture wrote to stderr"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor. A fixture that stops running its checks otherwise
    # passes by asserting nothing, which for a decision engine is the worst
    # available failure.
    if [ -z "$n" ] || [ "$n" -lt 140 ]; then
        fail "only ${n:-0} checks ran, wanted at least 140"
    else
        pass "$n checks"
    fi
fi

# ---------------------------------------------------------------------------
# THE LOAD-BEARING TIER.
printf 'TIER agreement: the authored stack and its condensed form\n'
SEEDS="${RULES_SEEDS:-2000}"
if ! timeout -k 5 600 ./gbasic tests/rules_agreement.bas "$SEEDS" >"$work/ag" 2>"$work/agerr"; then
    cat "$work/agerr"; fail "the agreement property did not run"
else
    d=$(sed -n 's/^disagreements: //p' "$work/ag")
    compared=$(sed -n 's/^target results compared: //p' "$work/ag")
    if [ "$d" != "0" ]; then
        grep '^DISAGREE' "$work/ag" | head -5
        fail "the condensed form decides differently ($d of $compared)"
    elif [ -z "$compared" ] || [ "$compared" -lt "$SEEDS" ]; then
        fail "only ${compared:-0} target results compared, wanted at least $SEEDS"
    else
        pass "$compared target results, 0 disagreements over $SEEDS generated stacks"
    fi

    # THE CENSUS. Without it, agreement is satisfied by a generator that only
    # ever produces one state -- which is what the first version did.
    census_ok=1
    check_floor() {
        v=$(sed -n "s/^$1: //p" "$work/ag")
        if [ -z "$v" ] || [ "$v" -lt "$2" ]; then
            fail "census: $1 was ${v:-0}, wanted at least $2 -- the population is not exercising this case"
            census_ok=0
        fi
    }
    # FLOORS SCALE WITH THE SEED COUNT, because a floor written for the
    # default silently becomes unmeetable the first time somebody runs the
    # suite smaller -- which reports a population problem where there is none,
    # the false-positive direction that gets a tripwire switched off.
    frac() { echo $(( SEEDS * $1 / 100 )); }
    check_floor "census ok" "$(frac 5)"
    check_floor "census absent unknown" "$(frac 10)"
    check_floor "census absent nothing" "$(frac 3)"
    check_floor "census error" "$(frac 10)"
    check_floor "conditions that fired" "$(frac 5)"
    check_floor "conditions that did not" "$(frac 10)"
    check_floor "unconditional overrides" "$(frac 3)"
    [ "$census_ok" = 1 ] && pass "the population reaches all three states, both absences, and rules that fire and do not"
fi

# ---------------------------------------------------------------------------
# THE CAP MUST SIT WELL BELOW WHERE THE INTERPRETER DIES, and "well below" is
# a measurement rather than a feeling. A gBASIC segfault produces NO
# diagnostic, NO output and exit 139, so the only honest way to know is to run
# the walk with the cap lifted and watch.
printf 'TIER the depth cap is measured against the crash, not chosen\n'
# NOT `gbasic -e '...'`: gbasic with no FILE argument starts the prompt and
# reads stdin, so the obvious one-liner hangs the suite rather than failing it.
cat >"$work/cap.bas" <<'BAS'
load rules
print string(rules.max_depth())
BAS
cap=$(timeout -k 5 30 ./gbasic "$work/cap.bas" </dev/null | tail -1)
mkdir -p "$work/lifted"
cp stdlib/*.bas "$work/lifted/"
sed -i 's/^        return 100$/        return 100000/' "$work/lifted/rules.bas"
cat >"$work/nest.bas" <<'BAS'
load rules
program main(args)
  n = number(args[0])
  e = rules.val(1)
  i = 0
  while i < n
    e = rules.op_of("+", [e, rules.val(1)])
    i = i + 1
  end while
  r = rules.evaluate(rules.stack_of({ t: e }, []), {})
  print "state " + r.targets["t"].state
end program
BAS
twice=$(( cap * 2 ))
if GBASIC_PATH="$work/lifted" timeout -k 5 180 ./gbasic --line-buffered "$work/nest.bas" "$twice" >"$work/lift" 2>&1 \
   && grep -qx 'state ok' "$work/lift"; then
    pass "with the cap lifted the interpreter survives $twice levels, twice the shipped cap of $cap"
else
    printf '    exit=%s output=[%s]\n' "$?" "$(cat "$work/lift")"
    fail "the interpreter cannot survive twice the cap ($twice) -- the cap is too close to the crash"
fi
# And the shipped cap refuses rather than crashing, at the boundary and far
# past it. A bound that might not fire is not a bound.
# THE LOOP STOPS AT 3,000 FOR A REASON THAT IS NOT THIS LIBRARY'S, and it is
# worth stating rather than quietly choosing a smaller number: a nested gBASIC
# VALUE around 40,000 deep segfaults the interpreter AT TEARDOWN -- measured with
# no library involved at all, the program prints its answer, succeeds, and then
# exit 139. So a tree that deep cannot be held by any gBASIC program whatever
# this library decides about it.
#
# THE CONSEQUENCE IS A REQUIREMENT ON THE UNBUILT DESERIALIZER (door 4): it must
# refuse by depth WHILE PARSING, before the tree exists, because once the value
# has been constructed a refusal is too late -- merely letting it go out of
# scope is the crash. `validate` refusing it is necessary and not sufficient.
for n in 101 3000; do
    if GBASIC_PATH=stdlib timeout -k 5 180 ./gbasic --line-buffered "$work/nest.bas" "$n" >"$work/ref" 2>&1 \
       && grep -qx 'state error' "$work/ref"; then
        pass "$n levels is refused, not crashed"
    else
        printf '    exit=%s output=[%s]\n' "$?" "$(cat "$work/ref")"
        fail "$n levels did not come back as a refusal"
    fi
done

# ---------------------------------------------------------------------------
# TWO STATEMENTS OF ONE WHITELIST DRIFT, and the direction that drifts SILENTLY
# is a key the dispatch honours and the registry does not list: validation
# would refuse it while the evaluator would run it, or the reverse. Neither is
# visible to any behavioural check that only uses registered names.
printf 'TIER the whitelist is one list, read from both places it is written\n'
dispatch=$(sed -n '/^    function _apply_call/,/^    end function/p' stdlib/rules.bas \
           | grep -oE 'key = "[a-z_]+"' | sed 's/key = "//; s/"//' | sort -u)
declared=$(sed -n '/^    function registry/,/^    end function/p' stdlib/rules.bas \
           | grep -oE 'key: "[a-z_]+"' | sed 's/key: "//; s/"//' | sort -u)
if [ -z "$dispatch" ] || [ -z "$declared" ]; then
    fail "could not read one of the two lists (dispatch=[$dispatch] declared=[$declared])"
elif [ "$dispatch" != "$declared" ]; then
    printf '    dispatch: %s\n    declared: %s\n' "$(echo $dispatch)" "$(echo $declared)"
    fail "the dispatch and the registry disagree about what may be called"
else
    pass "both lists are: $(echo $dispatch)"
fi

# ---------------------------------------------------------------------------
# DOOR 1, ASSERTED STRUCTURALLY, because no behavioural check can see it. The
# claim is not that a blacklist catches dangerous names -- it is that there is
# NO PATH from a rule to name resolution at all, so the library may not contain
# one. `bound` would turn a stored function value into a call, and a dynamic
# `lib.fn` built from a string is the other route.
printf 'TIER no route from a rule to gBASIC name resolution\n'
# STRIP THE COMMENTS FIRST. The first draft of this scanner reported the
# library's own ARGUMENT for the rule as a breach of it -- two comment lines
# that say "no `bound()`" and "bound() is arbitrary execution with extra
# steps". A tripwire whose false positives look exactly like its true ones is
# one somebody turns off, which is run_libcurl_floor.sh's lesson in a different
# language: a gBASIC comment runs from a leading `'` to end of line.
code_only() { sed "s/^[[:space:]]*'.*$//" "$1"; }
offenders=$(code_only stdlib/rules.bas | grep -nE '\bbound[[:space:]]*\(' || true)
if [ -n "$offenders" ]; then
    printf '    %s\n' "$offenders"
    fail "the library calls bound(): a stored function value becomes executable"
else
    pass "no bound() in the library"
fi
# THE CONTROL: the tripwire must FLAG a planted use, or "no offenders" is
# equally satisfied by a scanner that read nothing.
cp stdlib/rules.bas "$work/planted.bas"
printf '\n' >> "$work/planted.bas"
sed -i 's/^        if key = "min" then$/        if key = "min" then\n            r = bound(min, {})/' "$work/planted.bas"
# CAPTURED INTO A VARIABLE, NOT `| grep -q`, AND THIS COST A RUN TO FIND.
# Under `pipefail`, `code_only FILE | grep -q PATTERN` fails THE WHOLE PIPELINE
# when grep MATCHES: grep -q exits on the first hit and closes the pipe, `sed`
# is still writing and dies of SIGPIPE at 141, and pipefail takes the worst
# status. So the control reported "the scanner did not flag a planted bound()"
# against a scanner that had just flagged it -- a tier reporting the wrong
# cause, for the third time in this tree from the same shell option
# (run_ldap.sh's skip probe and run_web_tls.sh's, both via a deliberate
# nonzero; this one via SIGPIPE, which is the variant neither of those notes
# would have warned about).
planted_hits=$(code_only "$work/planted.bas" | grep -cE '\bbound[[:space:]]*\(' || true)
if [ "${planted_hits:-0}" -ge 1 ]; then
    pass "and the scanner flags a planted one"
else
    fail "the scanner did not flag a planted bound() -- it is measuring nothing"
fi

# ---------------------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/rules_test.bas >/dev/null 2>"$work/vg"; then
        pass "no definite leak or invalid access"
    else
        grep -E "definitely lost|Invalid" "$work/vg" | head -3; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_rules.sh\n'
