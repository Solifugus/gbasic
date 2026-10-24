#!/bin/bash
# `serialize` VERSION 3 -- the portable value encoding (docs/gsql_storage_design.md
# §3.3). Rows on disk, G-SQL's client protocol and the public builtin share it,
# and it closes exact_number_design.md's increment 4.
#
# Versions 1 and 2 copied integers in the MACHINE's byte order, justified by
# "round-trips are always same-binary" -- true for an actor and false for a file,
# which is copied between machines and outlives the binary that wrote it. And a
# number carried only its double, so 2^53 + 1 came back as 2^53.
#
# TIERS: SEMANTICS (the self-checking fixture: exactness, every kind, pinned
# bytes, a v2 payload the pre-v3 binary really wrote, corruption, the actor
# wire); ORACLE (an independent Python decoder written from the layout, because
# every other check reads back through our own `deserialize`, and a writer and
# reader agreeing on a wrong layout pass all of them); REGISTERED (a registered
# currency's NUMBER depends on registration order, so a payload written in one
# process must be found by its CODE in another -- two processes, so it lives
# here); FRAME (`frame` groups by serialization, and v3 recording exactness must
# not split an exact 3 from a computed 3.0); VALGRIND (over the corruption
# battery, every prefix of a payload holding every kind).
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
if ! make >/dev/null; then
    printf 'FAIL run_serialize (the build failed, so nothing below would test this tree)\n'
    exit 1
fi
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# A fixture that dies prints its own MISMATCH lines first; show them rather
# than only the death, which would hide the line that names the problem.
report() {
    local name="$1" out="$2" rc="$3" floor="$4"
    local bad checks
    bad=$(grep -c '^MISMATCH' "$out" || true)
    checks=$(sed -n 's/^ok  *\([0-9]*\) checks.*/\1/p' "$out" | tail -1)
    if [ "${bad:-0}" != "0" ]; then
        grep '^MISMATCH' "$out" | head -8
        fail "$name: $bad mismatches"
    elif [ "$rc" != "0" ]; then
        tail -3 "$out"
        fail "$name: the fixture exited $rc"
    elif [ "${checks:-0}" -lt "$floor" ]; then
        fail "$name: only ${checks:-0} checks ran -- the fixture stopped early"
    else
        pass "$name: $checks checks, 0 mismatches"
    fi
}

printf 'TIER semantics\n'
timeout -k 5 120 ./gbasic tests/serialize/serialize_test.bas > "$work/sem.out" 2> "$work/sem.err"
report "serialize_test" "$work/sem.out" "$?" 75
if [ -s "$work/sem.err" ]; then
    head -3 "$work/sem.err"; fail "the fixture wrote to stderr"
fi

printf 'TIER oracle: an independent decoder written from the layout\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP (no python3 -- the layout was cross-checked by NOTHING but our own reader)\n'
else
    ./gbasic tests/serialize/serialize_test.bas dump > "$work/dump.txt" 2>&1
    python3 -I tests/serialize/decode_v3.py < "$work/dump.txt" > "$work/oracle.out"
    orc=$?
    summary=$(tail -1 "$work/oracle.out")
    count=$(printf '%s' "$summary" | sed -n 's/^decoded \([0-9]*\) payloads.*/\1/p')
    if [ "$orc" != "0" ]; then
        grep '^MISMATCH' "$work/oracle.out" | head -8
        fail "the independent decoder disagrees ($summary)"
    elif [ "${count:-0}" -lt 25 ]; then
        fail "the decoder read only ${count:-0} payloads -- the dump stopped early"
    else
        pass "$summary"
    fi
fi

printf 'TIER registered: a currency is found by its code, not its number\n'
timeout -k 5 30 ./gbasic tests/serialize/serialize_test.bas write_reg "$work/reg.hex" > "$work/w.out" 2>&1
timeout -k 5 30 ./gbasic tests/serialize/serialize_test.bas read_reg "$work/reg.hex" > "$work/r.out" 2>&1
report "registered currency across two processes" "$work/r.out" "$?" 2

printf 'TIER frame: grouping is by `=`, which does not ask about exactness\n'
cat > "$work/frame.bas" <<'BAS'
load frame
function n_of(g)
    return len(g.v)
end function
program main()
    ' 6 / 2 is a computed, inexact 3; the literal 3 is exact. `=` says equal.
    df = { k: [3, 6 / 2, 3, 9007199254740993, 9007199254740992], v: [1, 2, 3, 4, 5] }
    s = frame.summarize(df, "k", { n: n_of })
    print string(len(s.k)) + " groups: " + string(s.n)
end program
BAS
got=$(./gbasic "$work/frame.bas" 2>&1)
# Three groups: {3, 3.0, 3}, and the two integers past 2^53, which `=` keeps
# apart. Two would mean the exact ones merged; four, that exactness split 3.
if [ "$got" = "3 groups: [3,1,1]" ]; then
    pass "an exact 3 and a computed 3.0 are one group, and 2^53 + 1 is not 2^53"
else
    printf '     %s\n' "$got"
    fail "frame.summarize grouped wrongly (want: 3 groups: [3,1,1])"
fi

printf 'TIER valgrind: every prefix of every kind, and the actor wire\n'
if ! command -v valgrind >/dev/null 2>&1; then
    printf '  SKIP (valgrind unavailable -- memory access was NOT measured)\n'
else
    vg_run ./gbasic tests/serialize/serialize_test.bas > "$work/vg.out" 2> "$work/vg.err"
    vrc=$?
    if [ "$vrc" = "$VG_EXIT" ]; then
        grep -E "Invalid|uninitialised|definitely" "$work/vg.err" | head -4
        fail "valgrind objected"
    elif [ "$vrc" != "0" ]; then
        tail -3 "$work/vg.out"
        fail "the fixture exited $vrc under valgrind"
    else
        pass "no definite leak or invalid access"
    fi
fi

if [ "$status" = "0" ]; then
    printf 'run_serialize: all checks passed\n'
else
    printf 'run_serialize: FAILED\n'
fi
exit "$status"
