#!/usr/bin/env bash
# TYPED VARIABLES: `x:number = e` locks x to a data type
# (docs/typed_variables_design.md, Matthew's rulings 2026-11-03).
#
# A plain `=` into a locked variable converts into the type or is refused,
# naming the declaration; `x:text = ...` relocks. The conversion is the brace
# modifier of the same name, so a lock and `{number}` cannot disagree.
#
# Tiers:
#   1. SEMANTICS -- tests/typed_lock_test.bas, self-checking. Each behaviour
#      sits beside its CONTROL on an unlocked variable (`y = "5"` stays text,
#      `u += "1"` is 421), so a lock that silently did nothing is a mismatch.
#      Its stderr must be exactly the one 2122 warning the unlocked control
#      raises: the locked comparisons beside it must not warn.
#   1b. PARAMETERS AND RETURN TYPES -- tests/typed_function_test.bas, the same
#      shape: `function f(a:number)` converts each argument (and default) and
#      refuses at the call; `function f(...):number` converts every `return`
#      and refuses at the return. A modifier used inside a typed function
#      keeps its own return.
#   2. PARSE-TIME REFUSALS -- a name that is not a data type is refused before
#      anything runs, and a foreign spelling (int, str, bool, USD) is answered
#      with the gBASIC one. Exact messages, nonzero exit, no output.
#   3. THE PROMPT -- a lock outlives the line that declared it, since the
#      prompt frees each line's AST (the lock is copied, not borrowed).
#   4. --ast shows the lock.
#   5. VALGRIND over the fixture.
#
# Headless, no network. Never skips (bar valgrind).
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_typed_locks: build failed\n'
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0

# --- Tier 1: semantics -------------------------------------------------------
if timeout 120 ./gbasic tests/typed_lock_test.bas >"$work/out" 2>"$work/err" </dev/null; then
    checks=$(sed -n 's/^checks: //p' "$work/out")
    if grep -q '^mismatches: 0$' "$work/out" && [ "${checks:-0}" -ge 55 ]; then
        printf 'PASS tests/typed_lock_test.bas (%s checks)\n' "$checks"
    else
        printf 'FAIL tests/typed_lock_test.bas -- lock semantics moved\n'
        grep -E '^MISMATCH|^checks|^mismatches' "$work/out"
        status=1
    fi
    ctl=$(grep -n 'control: with no lock, 1 = ' tests/typed_lock_test.bas | cut -d: -f1)
    if [ "$(wc -l <"$work/err")" -eq 1 ] && grep -q "typed_lock_test.bas:$ctl:.*\[2122\]\$" "$work/err"; then
        printf 'PASS only the unlocked control warns 2122; no locked comparison does\n'
    else
        printf 'FAIL stderr should be the one 2122 from the unlocked control at line %s:\n' "$ctl"
        cat "$work/err"
        status=1
    fi
else
    printf 'FAIL tests/typed_lock_test.bas (exit)\n'
    cat "$work/err"
    status=1
fi

# --- Tier 1b: parameters and return types -----------------------------------
if timeout 120 ./gbasic tests/typed_function_test.bas >"$work/fout" 2>"$work/ferr" </dev/null; then
    checks=$(sed -n 's/^checks: //p' "$work/fout")
    if grep -q '^mismatches: 0$' "$work/fout" && [ "${checks:-0}" -ge 19 ] && [ ! -s "$work/ferr" ]; then
        printf 'PASS tests/typed_function_test.bas (%s checks)\n' "$checks"
    else
        printf 'FAIL tests/typed_function_test.bas -- parameter or return types moved\n'
        grep -E '^MISMATCH|^checks|^mismatches' "$work/fout"
        cat "$work/ferr"
        status=1
    fi
else
    printf 'FAIL tests/typed_function_test.bas (exit)\n'
    cat "$work/ferr"
    status=1
fi

# --- Tier 2: parse-time refusals ----------------------------------------------
refuse() {
    local src=$1 want=$2
    printf '%s\nprint "ran"\n' "$src" >"$work/p.bas"
    if ./gbasic "$work/p.bas" >"$work/p.out" 2>"$work/p.err" </dev/null; then
        printf 'FAIL %s -- should not parse\n' "$src"
        status=1
    elif [ -s "$work/p.out" ]; then
        printf 'FAIL %s -- refused, but something ran first\n' "$src"
        status=1
    elif grep -qF "$want" "$work/p.err"; then
        printf 'PASS %-22s refused: %s\n' "${src%%$'\n'*}" "$want"
    else
        printf 'FAIL %s -- wanted "%s", got:\n' "$src" "$want"
        cat "$work/p.err"
        status=1
    fi
}
refuse 'x:int = 1'          "p.bas:1:3: 'int' is not a gBASIC type; write x:number"
refuse 'x:float = 1'        "'float' is not a gBASIC type; write x:number"
refuse 'x:str = 1'          "'str' is not a gBASIC type; write x:string"
refuse 'x:bool = true'      "'bool' is not a gBASIC type; write x:boolean"
refuse 'x:timestamp = 1'    "'timestamp' is not a gBASIC type; write x:datetime"
refuse 'x:USD = 1'          "a lock names a type, and money is one: write x:money(USD)"
refuse 'x:money = 1'        "money needs its currency: write x:money(USD)"
refuse 'x:money(usd) = 1'   "money takes a three-letter currency code, like money(USD), not 'usd'"
refuse 'x:number(2) = 1'    "syntax error"
refuse 'x:date(ISO) = 1'    "date takes no argument; only money does, as in money(USD)"
refuse 'x:array = 1'        "'array' cannot be a lock: a lock is for a data type"
refuse 'x:trimmed = 1'      "A lens such as {trimmed} is written in braces"
refuse 'function f(a:int)
end function'                "p.bas:1:14: 'int' is not a gBASIC type; write a:number"
refuse 'function f(a, b:bool = true)
end function'                "'bool' is not a gBASIC type; write b:boolean"
refuse 'function f():float
    return 1
end function'                "'float' is not a gBASIC type; write f(...):number"
refuse 'function o.m():USD
    return 1
end function'                "write o.m(...):money(USD)"

# A parameter lock on anything whose caller is the runtime is refused by name,
# as a default is: the clause, the command line or the URL supplies the value.
printf 'modifier shout(n:number) for assign\n    return value\nend modifier\nprint "ran"\n' >"$work/m.bas"
if ! ./gbasic "$work/m.bas" >"$work/m.out" 2>"$work/m.err" </dev/null \
   && grep -qF "modifier shout: parameter 'n' cannot be locked to a type -- only function parameters can" "$work/m.err" \
   && [ ! -s "$work/m.out" ]; then
    printf 'PASS a modifier parameter cannot be locked, and says why\n'
else
    printf 'FAIL a locked modifier parameter should be refused at load:\n'
    cat "$work/m.out" "$work/m.err"
    status=1
fi

# A space after the colon is the same lock (Python's style puts one there), and
# a label is still a label.
printf 'function f()\n    x: number = "4"\n    print x + 1\n    goto done\n    print "skipped"\ndone:\n    return "label"\nend function\nprint f()\n' >"$work/s.bas"
if [ "$(./gbasic "$work/s.bas" 2>&1)" = "$(printf '5\nlabel')" ]; then
    printf 'PASS x: number with a space locks, and name: on its own line is a label\n'
else
    printf 'FAIL a space after the colon, or a label beside a lock:\n'
    ./gbasic "$work/s.bas" 2>&1
    status=1
fi

# --- Tier 3: the prompt -------------------------------------------------------
mkdir -p "$work/session"
printf 'x:number = "4"\nx = "5"\nprint type(x)\n' \
    | GBASIC_SESSION_DIR="$work/session" timeout 30 ./gbasic --repl >"$work/r.out" 2>&1
if grep -qx 'number' "$work/r.out"; then
    printf 'PASS a lock declared on one prompt line holds on the next\n'
else
    printf 'FAIL the lock did not survive the prompt line that declared it:\n'
    cat "$work/r.out"
    status=1
fi

# --- Tier 4: --ast ------------------------------------------------------------
printf 'm:money(USD) = 1\nn:number = 2\nfunction f(a, b:money(EUR)):date\n    return 1\nend function\n' >"$work/a.bas"
./gbasic --ast "$work/a.bas" >"$work/a.out" 2>&1
if grep -q 'Assign lock(money(USD))' "$work/a.out" && grep -q 'Assign lock(number)' "$work/a.out" \
   && grep -q 'Parameters a b:money(EUR)$' "$work/a.out" && grep -q 'Returns date$' "$work/a.out"; then
    printf 'PASS --ast shows each lock\n'
else
    printf 'FAIL --ast does not show the lock:\n'
    cat "$work/a.out"
    status=1
fi

# --- Tier 5: valgrind ---------------------------------------------------------
if vg_available; then
    if vg_run ./gbasic tests/typed_lock_test.bas >"$work/vg.out" 2>"$work/vg.err" </dev/null \
       && grep -q '^mismatches: 0$' "$work/vg.out"; then
        printf 'PASS valgrind tests/typed_lock_test.bas\n'
    else
        printf 'FAIL valgrind tests/typed_lock_test.bas\n'
        tail -30 "$work/vg.err"
        status=1
    fi
    if vg_run ./gbasic tests/typed_function_test.bas >"$work/vgf.out" 2>"$work/vgf.err" </dev/null \
       && grep -q '^mismatches: 0$' "$work/vgf.out"; then
        printf 'PASS valgrind tests/typed_function_test.bas\n'
    else
        printf 'FAIL valgrind tests/typed_function_test.bas\n'
        tail -30 "$work/vgf.err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
