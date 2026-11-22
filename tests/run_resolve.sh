#!/usr/bin/env bash
# Names resolved once (docs/llvm_backend_design.md, increment 1): a call's
# name, and a qualified call's library, are looked up the first time the call
# site runs and kept on the node as a number (src/call_names.h), so the
# builtin chain in eval_call_rest -- and each module's chain of verbs -- tests
# integers, not text.
#
# MEASURED FIRST (2026-11-21, callgrind): strcmp was 50.6% of the instructions
# of a string-handling program and 45.0% of a record-handling one, nearly all
# of it the builtin chain: 37.7 million comparisons for about 300,000 builtin
# calls, some 125 a call. After: 1.96x and 1.71x fewer instructions on those
# two programs, and no comparison per call at all.
#
# Tiers:
#   list      src/call_names.h is sorted in C order and has no name twice: the
#             lookup is a binary search, and an unsorted list would MISS a
#             name silently -- the call falling through to "unknown function"
#             or, worse, to a later test of another name.
#   source    no `strcmp(expr->as.call.name, "...")` or
#             `strcmp(expr->as.call.library, "...")` is left in src/eval.c or
#             src/modules/*.c (a new one would put the cost back for every call
#             before it in the chain); a planted one is the control.
#   behaviour tests/resolve/builtins.bas: names early, in the middle and at the
#             end of the chain, the aggregate family, and a native module's
#             verb give the right answer.
#   cost      the same program at N and 2N passes under callgrind: the strcmp
#             calls made by the dispatch (eval_call, eval_call_rest,
#             call_name_lookup, crypto_ssh_call/_is, gbasic_builtin_function,
#             process_eval_call) must not grow with N -- at most one per 100
#             passes, where the old chain made over 600 a pass. Instruction
#             counts, so the machine's load does not matter.
#             (tests/resolve/strcmp_calls.py reads callgrind_annotate's tree.)
#   valgrind  the fixture under memcheck.
set -u
cd "$(dirname "$0")/.." || exit 1
make -s >/dev/null || { echo "FAIL build"; exit 1; }
. tests/valgrind_tier.sh
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "--- TIER list ---"
names=$(grep -oE '^CALL_NAME(_AS)?\([^)]*\)' src/call_names.h \
        | sed -E 's/^CALL_NAME\(([^)]*)\)$/\1/; s/^CALL_NAME_AS\([^,]*, "([^"]*)"\)$/\1/')
count=$(printf '%s\n' "$names" | wc -l)
if [ "$count" -lt 400 ]; then
    fail "only $count names read from src/call_names.h; the reader is measuring nothing"
elif ! printf '%s\n' "$names" | LC_ALL=C sort -c 2>"$tmp/sort.err"; then
    fail "src/call_names.h is not sorted in C order: $(cat "$tmp/sort.err")"
elif [ -n "$(printf '%s\n' "$names" | LC_ALL=C uniq -d)" ]; then
    fail "src/call_names.h names one twice: $(printf '%s\n' "$names" | LC_ALL=C uniq -d | tr '\n' ' ')"
else
    pass "$count names, sorted in C order, none twice"
fi
printf 'b\na\n' | LC_ALL=C sort -c 2>/dev/null && fail "CONTROL: the sort check passes an unsorted list" \
    || pass "CONTROL: an unsorted list is caught"

echo "--- TIER source ---"
scan() { grep -nE 'strcmp\(expr->as\.call\.(name|library), "' "$@" || true; }
left=$(scan src/eval.c src/modules/*.c)
if [ -z "$left" ]; then
    pass "no call's name or library is compared as text"
else
    printf '%s\n' "$left" | sed 's/^/    /'
    fail "a call's name compared as text: write CALL_IS(expr, name) or CALL_LIB_IS(expr, lib), and add the name to src/call_names.h"
fi
printf '    if (strcmp(expr->as.call.name, "len") == 0) {\n' > "$tmp/planted.c"
[ -n "$(scan "$tmp/planted.c")" ] && pass "CONTROL: a planted comparison is found" \
    || fail "CONTROL: the scan cannot see a planted comparison"

echo "--- TIER behaviour ---"
got=$(RESOLVE_N=3 ./gbasic tests/resolve/builtins.bas 2>&1)
if [ "$got" = "18" ]; then
    pass "lower, starts_with, mid, len, max and process.alive answer as before (18)"
else
    fail "the fixture printed '$got', not 18"
fi

echo "--- TIER cost ---"
if ! vg_available || ! command -v callgrind_annotate >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP cost (needs valgrind, callgrind_annotate and python3; NOT measured)\n'
else
    dispatch="eval_call eval_call_rest call_name_lookup call_name_id call_library_id crypto_ssh_call crypto_ssh_is gbasic_builtin_function process_eval_call"
    RESOLVE_N=2000 vg_callgrind_to "$tmp/cg.1" ./gbasic tests/resolve/builtins.bas
    RESOLVE_N=4000 vg_callgrind_to "$tmp/cg.2" ./gbasic tests/resolve/builtins.bas
    a=$(python3 tests/resolve/strcmp_calls.py "$tmp/cg.1" $dispatch)
    b=$(python3 tests/resolve/strcmp_calls.py "$tmp/cg.2" $dispatch)
    every=$(python3 tests/resolve/strcmp_calls.py "$tmp/cg.2" eval_call_rest env_find env_get)
    if [ -z "$a" ] || [ -z "$b" ] || [ "${every:-0}" -lt 1000 ]; then
        fail "the strcmp count read nothing ('$a', '$b', all callers '$every'); it is measuring nothing"
    elif [ $((b - a)) -le 20 ]; then
        pass "the dispatch's strcmp calls: $a at 2000 passes, $b at 4000 (none per call)"
    else
        fail "the dispatch's strcmp calls grow with the passes: $a at 2000, $b at 4000"
    fi
fi

echo "--- TIER valgrind ---"
if vg_available; then
    RESOLVE_N=50 vg_run ./gbasic tests/resolve/builtins.bas >/dev/null 2>"$tmp/vg.err" </dev/null
    if [ $? = "$VG_EXIT" ]; then
        sed 's/^/  /' "$tmp/vg.err"; fail "valgrind"
    else
        pass "valgrind (no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "PASS tests/run_resolve.sh" || echo "FAIL tests/run_resolve.sh"
exit $status
