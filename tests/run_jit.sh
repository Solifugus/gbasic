#!/usr/bin/env bash
# The compiler, increment 2 (docs/llvm_backend_design.md §4): under
# GBASIC_COMPILE=all each function's body is compiled by LLVM's ORC LLJIT on
# its first call (src/jit.c) and run as machine code. Step 1: that code calls
# the runtime for every statement (gbrt_stmt) and for what follows it
# (gbrt_after), the same two functions invoke_function's own loop calls.
# Step 2: `if`, `while` and `for ... to` are IR branches and loops, and the
# lists inside them unrolled the same way, each statement followed by
# gbrt_list_after, which eval_stmt_list calls too; a program block's body or
# the top level is compiled as such a list. Step 3: expressions, plain
# assignments and returns, each node a call the interpreted node makes too
# (or, for a number literal, its bytes). So nothing may differ but the speed.
# Increment 3: a function's locals that are numbers by construction are held
# in the compiled frame (GbSlot), and `+ - *` and the comparisons on two
# exact integers are done by the compiled code; the frame's variable is
# brought up to date before anything reads it by name. Steps 2-4: calls
# (plain, through a function value, a record's method, a library's
# function), `do`, `for each` and `-x` lowered.
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
#             off its end, exit() from inside a function), and every path out
#             of a lowered `if` or `while` (else if, break, continue, a
#             return, goto and exit from inside, a label inside a branch, a
#             raise absorbed inside a loop, a refused condition, a named
#             break that names no loop, a gosub from a loop): interpreted, it
#             prints flow.out and exits 3; compiled, stdout, stderr and the
#             status are the interpreter's, byte for byte.
#   held      tests/jit/held.bas takes every way a held number could part
#             from its variable (read before set, from a global, undefined;
#             read by name mid-loop; past int64 each way; a decimal; a
#             division; a lock; compound and typed assignments; a watch
#             elsewhere; a raise absorbed; goto; recursion; the counter
#             moved; exit): interpreted, held.out and held.err, status 3;
#             compiled, the same bytes.
#   calls     tests/jit/calls.bas takes every way a lowered call could part
#             from eval_call (nested and as operands; the count refused both
#             ways; an argument raising, the later ones and the body not run;
#             left to right, each once; defaults; a lock converting or
#             refusing at the call; parameters held only read, holding text,
#             a decimal, an array changed by the callee; builtins and a
#             function value; a raise in the callee with its trace; too deep;
#             warning 2102; exit inside a callee): interpreted, calls.out
#             and calls.err, status 4; compiled, the same bytes.
#   loops     tests/jit/loops.bas takes every way a lowered `do`, `for each`
#             or `-x` could part from the interpreted one (a `do`'s first
#             pass, continue reaching the condition, break, return, named
#             flow through it, a condition raising, unknown or text; a `for
#             each`'s index, flow, named flow freeing the inner array, return
#             twice deep, the snapshot, nothing to walk, its iterable read
#             after a held name is written, refusals, a raise in the body, a
#             file closed by a break, a csv source; `-x` on an exact integer,
#             the least one, a decimal, a double, money, a duration, unknown,
#             a call, text refused; `for each x:number, i` holding the
#             element, the index and the total they feed): interpreted,
#             loops.out and loops.err,
#             status 1; compiled, the same bytes.
#   values    tests/jit/values.bas takes every way a lowered call through a
#             function value, a record's method or a qualified call into a
#             gBASIC library could part from eval_call_rest (a value
#             reassigned, changing kind, bound, its count refused, shadowed
#             by a builtin, passed as a parameter, from a library; a method
#             changing `this`, calling a sibling, replaced, inherited,
#             refused, gained by a record at one site; a handle's verb; a
#             library's function in a loop, by its alias, behind a record
#             of its name and behind a handle of its name, missing; `this`
#             outside a method): interpreted, values.out and values.err,
#             status 1; compiled, the same bytes.
#   report    GBASIC_COMPILE=report's lines against tests/jit/flow.report:
#             each function compiled once, in the order first called, with
#             how many of its statements are lowered, the names it holds,
#             and how many times its body RAN. The count is kept by the compiled
#             body itself (its first instructions add one), so a body the
#             interpreter ran instead would not be counted: fact(10) is 10.
# Every run is bounded: a wrong jump in compiled code is as likely to loop as
# to print something wrong (a gosub's return dispatched one statement early
# re-ran the gosub, and the first perturbation of this suite hung it). The
# valgrind tier runs only after the bounded compiled run has passed.
#   valgrind  the compiled runs of flow.bas, held.bas, calls.bas,
#             loops.bas and values.bas under memcheck (the JIT is disposed at exit, or each
#             body's unwinding tables read as definitely lost), and
#             loops.bas interpreted (its loops' state is the compiled ones').
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

# the top level, compiled as one list when there is no program block
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/top.bas >"$tmp/ti.out" 2>"$tmp/ti.err" </dev/null; tirc=$?
GBASIC_COMPILE=all timeout 30 ./gbasic tests/jit/top.bas >"$tmp/tc.out" 2>"$tmp/tc.err" </dev/null; tcrc=$?
if cmp -s "$tmp/ti.out" tests/jit/top.out && cmp -s "$tmp/ti.err" tests/jit/top.err && [ "$tirc" = 1 ]; then
    pass "top level interpreted: top.out, top.err, status 1"
else
    fail "top level interpreted: status $tirc; $(diff "$tmp/ti.out" tests/jit/top.out | head -5) $(diff "$tmp/ti.err" tests/jit/top.err | head -3)"
fi
if cmp -s "$tmp/tc.out" "$tmp/ti.out" && cmp -s "$tmp/tc.err" "$tmp/ti.err" && [ "$tcrc" = "$tirc" ]; then
    pass "top level compiled: stdout, stderr and status $tcrc the interpreter's"
else
    compiled_ok=0
    fail "top level compiled: status $tcrc against $tirc; $(diff "$tmp/ti.out" "$tmp/tc.out" | head -5) $(diff "$tmp/ti.err" "$tmp/tc.err" | head -5)"
fi

# a program block's result: a named continue escaping every loop
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/stray.bas >"$tmp/si.out" 2>"$tmp/si.err" </dev/null; sirc=$?
GBASIC_COMPILE=all timeout 30 ./gbasic tests/jit/stray.bas >"$tmp/sc.out" 2>"$tmp/sc.err" </dev/null; scrc=$?
if cmp -s "$tmp/si.out" tests/jit/stray.out && cmp -s "$tmp/si.err" tests/jit/stray.err && [ "$sirc" = 1 ] &&
   cmp -s "$tmp/sc.out" "$tmp/si.out" && cmp -s "$tmp/sc.err" "$tmp/si.err" && [ "$scrc" = "$sirc" ]; then
    pass "a program block's escaping continue: raised, status 1, compiled as interpreted"
else
    fail "stray.bas: interpreted $sirc, compiled $scrc; $(cat "$tmp/sc.err" | head -2)"
fi

echo "--- TIER held ---"
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/held.bas >"$tmp/hi.out" 2>"$tmp/hi.err" </dev/null; hirc=$?
GBASIC_COMPILE=all timeout 30 ./gbasic tests/jit/held.bas >"$tmp/hc.out" 2>"$tmp/hc.err" </dev/null; hcrc=$?
if cmp -s "$tmp/hi.out" tests/jit/held.out && cmp -s "$tmp/hi.err" tests/jit/held.err && [ "$hirc" = 3 ]; then
    pass "interpreted: held.out, held.err, status 3"
else
    fail "held interpreted: status $hirc; $(diff "$tmp/hi.out" tests/jit/held.out | head -5) $(diff "$tmp/hi.err" tests/jit/held.err | head -3)"
fi
if cmp -s "$tmp/hc.out" "$tmp/hi.out" && cmp -s "$tmp/hc.err" "$tmp/hi.err" && [ "$hcrc" = "$hirc" ]; then
    pass "compiled: stdout, stderr and status $hcrc the interpreter's"
else
    compiled_ok=0
    fail "held compiled: status $hcrc against $hirc; $(diff "$tmp/hi.out" "$tmp/hc.out" | head -5) $(diff "$tmp/hi.err" "$tmp/hc.err" | head -5)"
fi
# what is held is decided once, when the body is compiled (Q7 (b)): a name
# built from a call is not held, nor one assigned text; a body with a
# gosub holds nothing (flow.report's `subs`, held by the report tier)
GBASIC_COMPILE=report timeout 30 ./gbasic tests/jit/held.bas >/dev/null 2>"$tmp/hr.err" </dev/null
if grep -qx 'compile: sum_loop: 4 statements, 4 lowered, 7 expressions, holds i n s, ran 1 time' "$tmp/hr.err" &&
   grep -qx 'compile: rec: 4 statements, 4 lowered, 11 expressions, holds n, ran 11 times' "$tmp/hr.err" &&
   grep -qx 'compile: texty: 5 statements, 5 lowered, 10 expressions, holds n, ran 1 time' "$tmp/hr.err" &&
   grep -qx 'compile: typed_local: 6 statements, 4 lowered, 7 expressions, holds k y, ran 1 time' "$tmp/hr.err"; then
    pass "held: i, s and the parameter n in sum_loop, n alone in rec (acc is built from a call), n but not t in texty, y declared :number"
else
    fail "what is held: $(grep -E 'sum_loop|rec|texty|typed_local' "$tmp/hr.err")"
fi

echo "--- TIER calls ---"
GBASIC_COMPILE=0 timeout 60 ./gbasic tests/jit/calls.bas >"$tmp/ci.out" 2>"$tmp/ci.err" </dev/null; circ=$?
GBASIC_COMPILE=all timeout 60 ./gbasic tests/jit/calls.bas >"$tmp/cc.out" 2>"$tmp/cc.err" </dev/null; ccrc=$?
if cmp -s "$tmp/ci.out" tests/jit/calls.out && cmp -s "$tmp/ci.err" tests/jit/calls.err && [ "$circ" = 4 ]; then
    pass "interpreted: calls.out, calls.err, status 4"
else
    fail "calls interpreted: status $circ; $(diff "$tmp/ci.out" tests/jit/calls.out | head -5) $(diff "$tmp/ci.err" tests/jit/calls.err | head -3)"
fi
if cmp -s "$tmp/cc.out" "$tmp/ci.out" && cmp -s "$tmp/cc.err" "$tmp/ci.err" && [ "$ccrc" = "$circ" ]; then
    pass "compiled: stdout, stderr and status $ccrc the interpreter's"
else
    compiled_ok=0
    fail "calls compiled: status $ccrc against $circ; $(diff "$tmp/ci.out" "$tmp/cc.out" | head -5) $(diff "$tmp/ci.err" "$tmp/cc.err" | head -5)"
fi

echo "--- TIER loops ---"
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/loops.bas >"$tmp/li.out" 2>"$tmp/li.err" </dev/null; lirc=$?
GBASIC_COMPILE=all timeout 30 ./gbasic tests/jit/loops.bas >"$tmp/lc.out" 2>"$tmp/lc.err" </dev/null; lcrc=$?
if cmp -s "$tmp/li.out" tests/jit/loops.out && cmp -s "$tmp/li.err" tests/jit/loops.err && [ "$lirc" = 1 ]; then
    pass "interpreted: loops.out, loops.err, status 1"
else
    fail "loops interpreted: status $lirc; $(diff "$tmp/li.out" tests/jit/loops.out | head -5) $(diff "$tmp/li.err" tests/jit/loops.err | head -3)"
fi
if cmp -s "$tmp/lc.out" "$tmp/li.out" && cmp -s "$tmp/lc.err" "$tmp/li.err" && [ "$lcrc" = "$lirc" ]; then
    pass "compiled: stdout, stderr and status $lcrc the interpreter's"
else
    compiled_ok=0
    fail "loops compiled: status $lcrc against $lirc; $(diff "$tmp/li.out" "$tmp/lc.out" | head -5) $(diff "$tmp/li.err" "$tmp/lc.err" | head -5)"
fi
# the loops are lowered, not run whole by the interpreter
GBASIC_COMPILE=report timeout 30 ./gbasic tests/jit/loops.bas >/dev/null 2>"$tmp/lr.err" </dev/null
if grep -qx 'compile: do_counts: 6 statements, 6 lowered, 19 expressions, holds i s, ran 1 time' "$tmp/lr.err" &&
   grep -qx 'compile: each_named: 9 statements, 7 lowered, 11 expressions, ran 1 time' "$tmp/lr.err"; then
    pass "a do and a nested for each lowered, the do's names held"
else
    fail "what is lowered: $(grep -E 'do_counts|each_named' "$tmp/lr.err")"
fi
# `for each x:number, i`: the element, the index and the total they feed held
# (an untyped element holds none of them: each_sum above)
if grep -qx 'compile: each_typed: 4 statements, 4 lowered, 7 expressions, holds i total x, ran 1 time' "$tmp/lr.err" &&
   grep -q 'compile: each_sum: .*expressions, ran 1 time$' "$tmp/lr.err"; then
    pass "a typed element and its index held, and the total; untyped, none"
else
    fail "typed element: $(grep -E 'each_typed|each_sum' "$tmp/lr.err")"
fi

echo "--- TIER values ---"
GBASIC_COMPILE=0 timeout 30 ./gbasic tests/jit/values.bas >"$tmp/vi.out" 2>"$tmp/vi.err" </dev/null; virc=$?
GBASIC_COMPILE=all timeout 60 ./gbasic tests/jit/values.bas >"$tmp/vc.out" 2>"$tmp/vc.err" </dev/null; vcrc=$?
if cmp -s "$tmp/vi.out" tests/jit/values.out && cmp -s "$tmp/vi.err" tests/jit/values.err && [ "$virc" = 1 ]; then
    pass "interpreted: values.out, values.err, status 1"
else
    fail "values interpreted: status $virc; $(diff "$tmp/vi.out" tests/jit/values.out | head -5) $(diff "$tmp/vi.err" tests/jit/values.err | head -3)"
fi
if cmp -s "$tmp/vc.out" "$tmp/vi.out" && cmp -s "$tmp/vc.err" "$tmp/vi.err" && [ "$vcrc" = "$virc" ]; then
    pass "compiled: stdout, stderr and status $vcrc the interpreter's"
else
    compiled_ok=0
    fail "values compiled: status $vcrc against $virc; $(diff "$tmp/vi.out" "$tmp/vc.out" | head -5) $(diff "$tmp/vi.err" "$tmp/vc.err" | head -5)"
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
if grep -qx 'compile: fact: 3 statements, 3 lowered, 10 expressions, holds n, ran 10 times' "$tmp/r.err" &&
   grep -qx 'compile: handled: 5 statements, 3 lowered, 5 expressions, holds x y, ran 2 times' "$tmp/r.err"; then
    pass "fact ran 10 times compiled, handled 2"
else
    fail "the compiled bodies' own counts: $(grep -E 'fact|handled' "$tmp/r.err")"
fi
# a program block and the top level are compiled as whole lists, each once
GBASIC_COMPILE=report timeout 30 ./gbasic tests/jit/top.bas >/dev/null 2>"$tmp/tr.err" </dev/null
if grep -qx 'compile: program main: 34 statements, 4 lowered, 4 expressions, ran 1 time' "$tmp/r.err" &&
   grep -qx 'compile: (top level): 25 statements, 14 lowered, 28 expressions, ran 1 time' "$tmp/tr.err"; then
    pass "program main and the top level ran compiled, once each"
else
    fail "the compiled lists: $(grep -E 'program|top level' "$tmp/r.err" "$tmp/tr.err")"
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
    GBASIC_COMPILE=all vg_run ./gbasic tests/jit/held.bas >/dev/null 2>"$tmp/vgh.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vgh.err" | tail -30; fail "valgrind: held.bas compiled"
    else
        pass "valgrind: held.bas compiled (no definite leak or invalid access)"
    fi
    GBASIC_COMPILE=all vg_run ./gbasic tests/jit/calls.bas >/dev/null 2>"$tmp/vgc.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vgc.err" | tail -30; fail "valgrind: calls.bas compiled"
    else
        pass "valgrind: calls.bas compiled (no definite leak or invalid access)"
    fi
    GBASIC_COMPILE=all vg_run ./gbasic tests/jit/loops.bas >/dev/null 2>"$tmp/vgl.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vgl.err" | tail -30; fail "valgrind: loops.bas compiled"
    else
        pass "valgrind: loops.bas compiled (no definite leak or invalid access)"
    fi
    GBASIC_COMPILE=all vg_run ./gbasic tests/jit/values.bas >/dev/null 2>"$tmp/vgv.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vgv.err" | tail -30; fail "valgrind: values.bas compiled"
    else
        pass "valgrind: values.bas compiled (no definite leak or invalid access)"
    fi
    GBASIC_COMPILE=0 vg_run ./gbasic tests/jit/loops.bas >/dev/null 2>"$tmp/vgli.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vgli.err" | tail -30; fail "valgrind: loops.bas interpreted"
    else
        pass "valgrind: loops.bas interpreted (no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "PASS tests/run_jit.sh" || echo "FAIL tests/run_jit.sh"
exit $status
