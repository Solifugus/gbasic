#!/usr/bin/env bash
# WHAT A QBasic PROGRAMMER TYPES FIRST, and what gBASIC says back.
#
# `dim` has had a good answer since the beginning -- "`dim` is not a gBASIC
# statement; assign to create a variable (x = 0)" -- and the lexer comment beside
# it says why it is lexed as a keyword at all: "to be refused with advice where
# someone arriving from QBasic would type it." Every OTHER construct in that
# reader's first ten lines got `unexpected token`, or worse, advice about
# something else. That neighbouring inconsistency is what makes this a bug rather
# than a policy, the same argument run_silent_traps.sh made when `USD` raised
# four lines from the typed modifiers that printed.
#
# Measured 2026-09-30 against 0.3.0, before any of this:
#
#   7 mod 3    unknown duration unit 'mod' -- the units are year, month, week...
#   7 \ 2      unexpected token
#   2 ^ 3      unexpected token
#   7 % 3      unexpected token
#   a$ = "x"   '$' is not a money literal; write p(USD)= 19.99
#   a% = 5     unexpected token
#   "a" & "b"  unexpected token
#
# `7 mod 3` is the sharpest: MOD is an INFIX OPERATOR in QBasic, so it is what a
# reader of the Core Language book types first, and being answered with seven
# duration units names nothing they wrote. The reference already knew -- "`7 mod
# 2` is duration syntax, not modulo -- `mod` is a call" -- so the page was right
# and only the binary was unhelpful.
#
# The `$` one is subtler and is the reason POSITION is a tier of its own: one
# character, two different mistakes. `$19.99` is a money guess; `a$` is a string
# sigil. Answering the second with `p(USD)= 19.99` names a remedy for a mistake
# the reader did not make.
#
# SELF-CHECKING, not golden: every one of these is a MESSAGE, and a golden
# records whatever the binary says as expected -- including advice that points at
# a function which does not exist. Which is exactly what the REMEDY tier is for.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

status=0
checks=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Run a snippet, return the diagnostic with the file:line:col prefix stripped.
# EVERY snippet here is SUPPOSED to fail, so the run's nonzero status must not
# reach `set -e`. Under `pipefail` an unguarded `got=$(...)` on a failing pipeline
# ends the suite SILENTLY, with a green-looking scrollback and no FAIL line --
# which is exactly what the first version of this file did, and the trap
# run_http.sh records from the other direction.
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { ./gbasic "$tmp/p.bas" 2>&1 || true; } | head -1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //'
}

want() {   # want <label> <source> <substring>
    checks=$((checks + 1))
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want substring: %s\n  got:            %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}

lacks() {  # lacks <label> <source> <substring-that-must-be-absent>
    checks=$((checks + 1))
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'MISMATCH %s\n  must NOT contain: %s\n  got:              %s\n' "$1" "$3" "$got"; status=1 ;;
        *) printf 'ok   %s\n' "$1" ;;
    esac
}

echo "--- TIER 1: the operators name a call ---"
want 'mod word'      'print 7 mod 3'      'the remainder is mod(a, b)'
want 'MOD spelling'  'print 7 MOD 3'      "'MOD' is not an operator"
want 'xor word'      'print 7 xor 3'      'bxor(a, b)'
want 'div word'      'print 7 div 3'      'floor(a / b)'
want 'backslash'     'print 7 \ 2'        'integer division is floor(a / b)'
want 'caret'         'print 2 ^ 3'        'exponentiation is pow(a, b)'
want 'percent op'    'print 7 % 3'        'the remainder is mod(a, b)'
want 'ampersand'     'print "a" & "b"'    'does not join text; use +'

echo "--- TIER 2: the five type sigils ---"
for pair in 'a$ = "x":$' 'a% = 5:%' 'a! = 5:!' 'a# = 5:#' 'a& = 5:&'; do
    src="${pair%:*}"; ch="${pair##*:}"
    want "sigil $ch" "$src" "'$ch' is a type sigil; gBASIC has none"
done

echo "--- TIER 3: POSITION, asserted as a difference ---"
# One character, two mistakes. Either assertion ALONE passes on a build that
# always gives the same answer, so both are required and they must DIFFER.
want  'dollar after name' 'a$ = "x"'   'type sigil'
lacks 'dollar after name is not money advice' 'a$ = "x"' 'p(USD)'
want  'dollar before digits' 'p = $19.99' 'is not a money literal'
lacks 'dollar before digits is not sigil advice' 'p = $19.99' 'type sigil'
want  'percent after name' 'a% = 5'     'type sigil'
lacks 'percent after name is not operator advice' 'a% = 5' 'the remainder'
want  'percent alone'      'print 7 % 3' 'the remainder'
lacks 'percent alone is not sigil advice' 'print 7 % 3' 'type sigil'

echo "--- TIER 4: EVERY REMEDY A MESSAGE NAMES MUST RUN ---"
# THE LOAD-BEARING TIER. web.configure shipped a refusal naming a remedy that
# did not exist, and a message is the one kind of code nothing executes -- so a
# hint can rot into a lie with every other tier green.
#
# DERIVED FROM THE MESSAGES, never a hand-written list: the names are scraped out
# of what the binary just said, so renaming a remedy to something that does not
# exist fails HERE rather than being quietly re-pinned in a golden.
remedies="$(for src in 'print 7 mod 3' 'print 7 xor 3' 'print 7 div 3' \
                       'print 7 \ 2' 'print 2 ^ 3' 'print 7 % 3'; do
    say "$src"
done | grep -o '[a-z_][a-z_0-9]*(a[^)]*)' | sed 's/(.*//' | sort -u)"
remedy_count="$(printf '%s\n' "$remedies" | grep -c . || true)"
if [ "${remedy_count:-0}" -lt 4 ]; then
    # A scraper that matches nothing reports a clean run.
    printf 'MISMATCH remedy scrape found only %s names -- it stopped matching, it did not pass\n' "$remedy_count"
    status=1
else
    # The loop body runs in a SUBSHELL (it is fed by a pipe), so a failure inside
    # it cannot set `status` -- the first version printed its summary `ok` line
    # underneath a MISMATCH it had just reported. The verdict goes through a file.
    : >"$tmp/remedy_fail"
    printf '%s\n' "$remedies" | while IFS= read -r fn; do
        [ -n "$fn" ] || continue
        case "$fn" in
            floor) call="$fn(7 / 2)" ;;
            *)     call="$fn(7, 3)" ;;
        esac
        printf 'program main(args)\n  print(string(%s))\nend program\n' "$call" >"$tmp/r.bas"
        if out="$({ ./gbasic "$tmp/r.bas" 2>&1 || true; })" && [ -n "$out" ] \
           && ! printf '%s' "$out" | grep -q 'error'; then
            printf 'ok   remedy %s runs -> %s\n' "$fn" "$out"
        else
            printf 'MISMATCH remedy %s is NAMED BY A DIAGNOSTIC AND DOES NOT RUN: %s\n' "$fn" "$out"
            echo "$fn" >>"$tmp/remedy_fail"
        fi
        # ...and has_builtin must agree, since the reference tells programs to
        # probe before calling. `mod` answered FALSE here until 2026-09-30, which
        # would have made this whole file's advice unprobeable.
        printf 'program main(args)\n  if not has_builtin("%s") then print("no")\nend program\n' "$fn" >"$tmp/h.bas"
        if [ -n "$({ ./gbasic "$tmp/h.bas" 2>&1 || true; })" ]; then
            printf 'MISMATCH remedy %s is named by a diagnostic but has_builtin says false\n' "$fn"
            echo "$fn" >>"$tmp/remedy_fail"
        fi
    done
    checks=$((checks + remedy_count * 2))
    if [ -s "$tmp/remedy_fail" ]; then
        status=1
    else
        printf 'ok   all %s scraped remedies run and answer has_builtin\n' "$remedy_count"
    fi
fi

echo "--- TIER 5: CONTROLS -- what must NOT have changed ---"
# Without these, every tier above is satisfied by a build that refuses more.
want 'duration 3 days'     'print 3 days'        '3 days'
want 'duration 1 hour'     'print 1 hour'        '1 hour'
want 'duration 90 minutes' 'print 90 minutes'    '90 minutes'
want 'date + duration'     'd {date}= "2026-03-15"
print d + 5 days'                                '2026-03-20'
# A genuinely unknown unit keeps the unit message -- the operator table must not
# become "any word after a number gets advice".
want 'unknown unit kept'   'print 7 fortnights'  'unknown duration unit'
want 'unknown unit ms'     'print 7 ms'          'unknown duration unit'
# shl/shr are deliberately NOT in the table: gBASIC has no shift builtin, so
# there is nothing to name. This pins that as a decision, not an oversight.
want 'shl has no remedy'   'print 7 shl 1'       'unknown duration unit'
# The operators that DO exist, and the money modifier the $ message points at.
want 'not-equal operator'  'a = 1
b = 2
print a != b'                                    'true'
want 'USD modifier works'  'p {USD}= 19.99
print p'                                         '19.99'
want 'exponent literal'    'print 1e20'          '1e+20'
want 'dim still advises'   'DIM a(5)'            'assign to create a variable'

echo "--- TIER 6: located, nonzero, nothing ran, and it reaches the sink ---"
for src in 'print 7 mod 3' 'print 7 \ 2' 'print 2 ^ 3' 'a$ = "x"' 'a% = 5' 'print "a" & "b"'; do
    checks=$((checks + 1))
    printf 'print "RAN"\n%s\n' "$src" >"$tmp/p.bas"
    out="$(./gbasic "$tmp/p.bas" 2>&1)" && rc=0 || rc=$?
    problems=""
    [ "$rc" -ne 0 ] || problems="$problems exit=0"
    printf '%s' "$out" | grep -qE 'p\.bas:[0-9]+:[0-9]+:' || problems="$problems unlocated"
    # `grep -qx && assign` returns grep's status, and grep NOT matching is the
    # PASSING case here -- so written that way it ends the suite under set -e.
    if printf '%s\n' "$out" | grep -qx 'RAN'; then problems="$problems something-ran"; fi
    # Same trap a third time: this run is MEANT to fail, so under pipefail the
    # assignment inherits the failure and set -e ends the suite before Tier 6
    # prints anything at all.
    js="$({ ./gbasic --json-diagnostics "$tmp/p.bas" 2>&1 >/dev/null || true; } | head -1)"
    printf '%s' "$js" | python3 -c 'import sys,json; json.loads(sys.stdin.read())' 2>/dev/null \
        || problems="$problems not-json-on-sink"
    if [ -n "$problems" ]; then
        printf 'MISMATCH properties for `%s`:%s\n' "$src" "$problems"; status=1
    else
        printf 'ok   properties `%s` (located, exit %d, nothing ran, sink is JSON)\n' "$src" "$rc"
    fi
done

printf '\nchecks: %d\n' "$checks"
if [ "$status" = 0 ]; then printf 'mismatches: 0\n'; else printf 'FAILED\n'; fi
exit "$status"
