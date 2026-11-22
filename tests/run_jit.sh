#!/usr/bin/env bash
# The compiler, increment 2 step 1 (docs/llvm_backend_design.md §4): under
# GBASIC_COMPILE=all each function's body is compiled by LLVM's ORC LLJIT on
# its first call (src/jit.c) and run as machine code. In step 1 that code
# calls the runtime for every statement (gbrt_stmt) and for what follows it
# (gbrt_after), the same two functions invoke_function's own loop calls, so
# nothing may differ but the speed -- and step 1 saves nothing yet.
#
# This suite holds the switch and the paths through a body. The whole of the
# behaviour is held by the gate's second lane, `GBASIC_COMPILE=all
# ./tests/run_all.sh`, every suite with every function compiled.
#
# Tiers:
#   built     `--modules` has a `compile` row. A build without LLVM must say,
#             under GBASIC_COMPILE=report, that nothing is compiled, and
#             every tier below skips saying so.
#   flow      tests/jit/flow.bas takes every path out of a statement the loop
#             decides (a return, recursion, goto back and forward, gosub
#             nested, a raise absorbed by goto next and sent to a label, a
#             goto to a label that does not exist, an empty body, a body run
#             off its end, exit() from inside a function): interpreted, it
#             prints flow.out and exits 3; compiled, stdout, stderr and the
#             status are the interpreter's, byte for byte.
#   report    GBASIC_COMPILE=report's lines against tests/jit/flow.report:
#             each function compiled once, in the order first called, with
#             how many times its body RAN. The count is kept by the compiled
#             body itself (its first instructions add one), so a body the
#             interpreter ran instead would not be counted: fact(10) is 10.
# Every run is bounded: a wrong jump in compiled code is as likely to loop as
# to print something wrong (a gosub's return dispatched one statement early
# re-ran the gosub, and the first perturbation of this suite hung it). The
# valgrind tier runs only after the bounded compiled run has passed.
#   valgrind  the compiled run under memcheck (the JIT is disposed at exit,
#             or each body's unwinding tables read as definitely lost).
set -u
cd "$(dirname "$0")/.." || exit 1
make -s >/dev/null || { echo "FAIL build"; exit 1; }
. tests/portable.sh
. tests/valgrind_tier.sh
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "--- TIER built ---"
row=$(./gbasic --modules | awk -F'\t' '$1 == "compile" { print $2 }')
case "$row" in
    built) pass "--modules: compile built" ;;
    "left out")
        GBASIC_COMPILE=report timeout 30 ./gbasic tests/jit/flow.bas >/dev/null 2>"$tmp/err" </dev/null
        if grep -qx 'compile: not available in this build (needs LLVM 18 or later); every function is interpreted' "$tmp/err" &&
           [ "$(grep -c '^compile:' "$tmp/err")" = 1 ]; then
            pass "--modules: compile left out, and GBASIC_COMPILE=report says so once"
        else
            fail "a build without LLVM: GBASIC_COMPILE=report said: $(grep '^compile:' "$tmp/err" | head -3)"
        fi
        printf 'SKIP flow, report, valgrind: this build has no LLVM, so nothing was compiled or measured\n'
        [ "$status" = 0 ] && echo "PASS tests/run_jit.sh" || echo "FAIL tests/run_jit.sh"
        exit $status ;;
    *) fail "--modules has no compile row (got '${row}')" ;;
esac

echo "--- TIER flow ---"
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/flow.bas >"$tmp/i.out" 2>"$tmp/i.err" </dev/null; irc=$?
GBASIC_COMPILE=all timeout 30 ./gbasic tests/jit/flow.bas >"$tmp/c.out" 2>"$tmp/c.err" </dev/null; crc=$?
if cmp -s "$tmp/i.out" tests/jit/flow.out && [ "$irc" = 3 ] && [ ! -s "$tmp/i.err" ]; then
    pass "interpreted: flow.out, status 3, nothing on stderr"
else
    fail "interpreted: status $irc; $(diff "$tmp/i.out" tests/jit/flow.out | head -5) $(head -3 "$tmp/i.err")"
fi
compiled_ok=0
if cmp -s "$tmp/c.out" "$tmp/i.out" && cmp -s "$tmp/c.err" "$tmp/i.err" && [ "$crc" = "$irc" ]; then
    compiled_ok=1
    pass "compiled: stdout, stderr and status $crc the interpreter's"
else
    fail "compiled: status $crc against $irc; $(diff "$tmp/i.out" "$tmp/c.out" | head -5) $(diff "$tmp/i.err" "$tmp/c.err" | head -5)"
fi

echo "--- TIER report ---"
GBASIC_COMPILE=report timeout 30 ./gbasic tests/jit/flow.bas >"$tmp/r.out" 2>"$tmp/r.err" </dev/null
if cmp -s "$tmp/r.err" tests/jit/flow.report && cmp -s "$tmp/r.out" tests/jit/flow.out; then
    pass "report: $(tail -1 "$tmp/r.err" | sed 's/^compile: //'), each body's runs counted by the body"
else
    fail "report differs from tests/jit/flow.report: $(diff tests/jit/flow.report "$tmp/r.err" | head -6)"
fi
# the count is the body's, so it must be the number of calls: fact(10) is
# ten, handled(2) and handled(0) two
if grep -qx 'compile: fact: 2 statements, 0 lowered, ran 10 times' "$tmp/r.err" &&
   grep -qx 'compile: handled: 5 statements, 0 lowered, ran 2 times' "$tmp/r.err"; then
    pass "fact ran 10 times compiled, handled 2"
else
    fail "the compiled bodies' own counts: $(grep -E 'fact|handled' "$tmp/r.err")"
fi

echo "--- TIER valgrind ---"
# vg_run is a function, which timeout cannot run: so only a compiled run that
# has already finished, bounded, above
if [ "$compiled_ok" = 0 ]; then
    printf 'SKIP valgrind: the compiled run failed above, and might not end\n'
elif vg_available; then
    GBASIC_COMPILE=all vg_run ./gbasic tests/jit/flow.bas >/dev/null 2>"$tmp/vg.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vg.err" | tail -30; fail "valgrind: flow.bas compiled"
    else
        pass "valgrind: flow.bas compiled (no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "PASS tests/run_jit.sh" || echo "FAIL tests/run_jit.sh"
exit $status
