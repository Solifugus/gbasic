#!/usr/bin/env bash
set -uo pipefail

# PLAT-OPTPARAM: literal default values for function parameters.
#
# `function f(a, b = 10)`. gBASIC had strict arity and no way to express an
# optional argument, so every library that wanted one paid for it in a second
# function name or an options record -- and the core finance design had to be
# built around the absence (docs/finance_design.md §6).
#
# LITERALS ONLY, AND THAT IS THE DESIGN RATHER THAN A SHORTCUT. An arbitrary
# default expression must be evaluated in SOME scope, and gBASIC has no
# closures: a default reading an enclosing variable would inherit the
# read-then-shadow rule run_core.sh exists for, and one reading an earlier
# parameter would need a defined evaluation order. A literal has nothing to
# see, so the question does not arise -- and literal -> expression is a change
# that can be made later, while the reverse is not.
#
# It also disposes of the classic mutable-default bug by construction: an array
# or record default is a PARSE ERROR and gBASIC strings are immutable, so no
# default is a value that could be mutated and carried into the next call.
#
# THE CONFLICT TIER IS NOT DECORATION. The project rejected `IDENT expression`
# as a statement form over 4 MEASURED shift/reduce conflicts, and the grammar
# has been at zero since. A parameter default introduces `IDENT OP_EQ` into a
# list that previously held bare identifiers, which is exactly the shape that
# could collide with assignment; the tier asserts it did not.
#
# Headless, GI-independent, never skips (bar valgrind).

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

checks=0
failures=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

# ---------------------------------------------------------------- semantics
printf 'TIER semantics\n'
if ./gbasic tests/optparams/semantics.bas >"$scratch/out" 2>"$scratch/err"; then
    pass "semantics exits 0"
else
    fail "semantics exits 0 ($(cat "$scratch/err"))"
fi
[ -s "$scratch/err" ] && fail "semantics writes nothing to stderr" \
                      || pass "semantics writes nothing to stderr"
if grep -q "^mismatches: 0$" "$scratch/out"; then
    pass "semantics reports no mismatch"
else
    fail "semantics reports no mismatch"
    grep MISMATCH "$scratch/out" | head -5
fi
# A coverage floor: a fixture that stopped running its checks would otherwise
# pass by asserting nothing.
ran=$(sed -n 's/^checks: //p' "$scratch/out")
if [ "${ran:-0}" -ge 12 ]; then
    pass "semantics ran at least 12 checks (ran ${ran:-0})"
else
    fail "semantics ran at least 12 checks (ran ${ran:-0})"
fi

# ----------------------------------------------------------------- refusals
printf 'TIER refusals\n'
# Each case pairs a fixture with the text its message must contain. Substring,
# not whole-stderr: a parse error carries a column that moves when the fixture
# is edited, and pinning it would make every comment change a rebaseline.
refuse() { # file expected-substring
    local f="tests/optparams/$1" want="$2"
    if ./gbasic "$f" >"$scratch/o" 2>"$scratch/e"; then
        fail "$1 (expected a NONZERO exit)"
        return
    fi
    if ! grep -qF "$want" "$scratch/e"; then
        fail "$1 (message: $(head -1 "$scratch/e"))"
        return
    fi
    # Nothing may run: a refusal that reports and then executes the program is
    # the shape run_parse_exit.sh exists for.
    if [ -s "$scratch/o" ]; then
        fail "$1 (the program ran anyway: $(head -1 "$scratch/o"))"
        return
    fi
    pass "$1"
}
refuse refuse_gap.bas        "optional parameters must come last"
refuse refuse_array.bas      "syntax error"
refuse refuse_record.bas     "syntax error"
refuse refuse_expression.bas "syntax error"
refuse refuse_ident.bas      "syntax error"
refuse refuse_call.bas       "syntax error"
refuse refuse_modifier.bas   "only function parameters can"
refuse refuse_program.bas    "only function parameters can"
refuse refuse_handler.bas    "only function parameters can"

# ------------------------------------------------------------------- actor
printf 'TIER across a process boundary\n'
# The child is a fork+exec that never saw the call site: the parent sends only
# the arguments the CALL supplied and the child fills the tail itself. Sending
# materialized defaults instead would put them on the wire and let a child
# built from different source disagree with its parent about what they are.
cat >"$scratch/actor.bas" <<'EOF'
function worker(a, b = 99)
    print "child sees " + string(a) + "," + string(b)
    return a
end function
h = spawn worker(1)
sleep(0.5)
EOF
if ./gbasic "$scratch/actor.bas" >"$scratch/out" 2>"$scratch/err" \
   && grep -q "child sees 1,99" "$scratch/out"; then
    pass "an actor child applies the default itself"
else
    fail "an actor child applies the default itself ($(cat "$scratch/out" "$scratch/err" | head -2))"
fi

# --------------------------------------------------------------- grammar
printf 'TIER the grammar stayed at zero conflicts\n'
if command -v bison >/dev/null 2>&1; then
    if bison -d src/parser.y -o "$scratch/p.tab.c" 2>"$scratch/bison.err"; then
        if grep -qi "conflict" "$scratch/bison.err"; then
            fail "zero shift/reduce conflicts ($(grep -i conflict "$scratch/bison.err" | head -1))"
        else
            pass "zero shift/reduce conflicts"
        fi
    else
        fail "the grammar builds"
    fi
else
    pass "zero shift/reduce conflicts (SKIP: no bison)"
fi

# --------------------------------------------------------------- valgrind
printf 'TIER valgrind\n'
if vg_available; then
    # The defaults array is a NEW allocation per parameter list, freed in six
    # places (function, modifier, program, watch, server item, modifier
    # signature). Missing one leaks a few bytes per parse -- which is exactly
    # what happened, and 22 suites went red on their own valgrind tiers.
    if vg_run ./gbasic tests/optparams/semantics.bas >/dev/null 2>"$scratch/vg"; then
        pass "no definite leak or invalid access"
    else
        fail "no definite leak or invalid access"
        grep -E "definitely lost|Invalid" "$scratch/vg" | head -3
    fi
else
    pass "valgrind (SKIP: not installed)"
fi

printf '\nrun_optparams: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ] || exit 1
