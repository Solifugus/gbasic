#!/usr/bin/env bash
# The gBASIC books' last gap batch (gbasic-books/docs/volume-3/gap-ledger.md,
# G31-G65, 2026-11-08): the core items, checked here; library items live in
# their own suites (intake, odbc, webclient, xml positions).
#
# TIER core: tests/book_gaps/core_test.bas, self-checking, every answer by hand
# (G62 exact exit codes, G39 exact byte_find, G56 money negation, G41 money
# refusal details, G36 runtime(), G64 process.alive/info, G61 json_decode,
# G37 default of a missing field).
#
# The oracles from OUTSIDE gBASIC, since a value checked only against itself
# passes whatever it is:
#   TIER version  -- runtime() against `gbasic --version`, which is the other
#                    copy of the version: main() hands it over, and the two
#                    drift the day only one is bumped.
#   TIER owner    -- permissions().owner/group and process.self().uid against
#                    `stat` and `id` (G65).
#   TIER trylock  -- lock(f, { wait: false }) against a second gbasic holding
#                    the lock: false while it holds it, true once it is gone,
#                    and never a wait (G42).
#   TIER valgrind -- bounded, as a hang must fail rather than block the gate.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "--- TIER core ---"
out="$(timeout -k 5 60 ./gbasic tests/book_gaps/core_test.bas 2>"$tmp/core.err" </dev/null)"
last="$(printf '%s\n' "$out" | tail -1)"
printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
n="$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')"
if [ -z "$n" ] || [ "$n" -lt 26 ]; then
    printf 'FAIL core (%s)\n' "${last:-no verdict}"; status=1
elif [ -s "$tmp/core.err" ]; then
    sed 's/^/  /' "$tmp/core.err" | head -5; printf 'FAIL core (the fixture wrote to stderr)\n'; status=1
else
    printf 'PASS core (%s checks)\n' "$n"
fi

echo "--- TIER version ---"
said="$(printf '%s\n' "$out" | sed -n 's/^RUNTIME //p')"
ver="$(./gbasic --version | sed 's/^gBASIC //')"
mine="$(printf '%s' "$said" | awk '{ if ($2 == "") print $1; else print $1 "+" $2 }')"
if [ -n "$said" ] && [ "$mine" = "$ver" ]; then
    printf 'PASS version (runtime() says %s, as --version does)\n' "$ver"
else
    printf 'FAIL version (runtime() says [%s], --version says [%s])\n' "$mine" "$ver"; status=1
fi

echo "--- TIER owner ---"
printf 'x\n' > "$tmp/owned.txt"
printf 'p = permissions("%s")\nme = process.self()\nprint string(p.owner) + " " + p.owner_name + " " + string(p.group) + " " + p.group_name + " " + string(me.uid) + " " + me.user\n' \
    "$tmp/owned.txt" > "$tmp/owner.bas"
got="$(./gbasic "$tmp/owner.bas" 2>&1)"
want="$(stat -c '%u %U %g %G' "$tmp/owned.txt") $(id -u) $(id -un)"
if [ "$got" = "$want" ]; then
    printf 'PASS owner (permissions and self agree with stat and id: %s)\n' "$got"
else
    printf 'FAIL owner (got [%s], stat and id say [%s])\n' "$got" "$want"; status=1
fi

echo "--- TIER trylock ---"
printf 'f {file}= "%s/job.lock"\nlock(f)\nprint "held"\nsleep(3)\n' "$tmp" > "$tmp/holder.bas"
printf 'f {file}= "%s/job.lock"\nprint lock(f, { wait: false })\n' "$tmp" > "$tmp/try.bas"
./gbasic --line-buffered "$tmp/holder.bas" > "$tmp/holder.out" 2>&1 &
holder=$!
for _ in $(seq 1 100); do grep -q held "$tmp/holder.out" 2>/dev/null && break; sleep 0.05; done
t0=$(date +%s%N); during="$(./gbasic "$tmp/try.bas" 2>&1)"; t1=$(date +%s%N)
wait "$holder"
after="$(./gbasic "$tmp/try.bas" 2>&1)"
waited=$(( (t1 - t0) / 1000000 ))
if [ "$during" = false ] && [ "$after" = true ] && [ "$waited" -lt 2000 ]; then
    printf 'PASS trylock (false while another process holds it, in %sms; true once it is gone)\n' "$waited"
else
    printf 'FAIL trylock (during [%s] in %sms, after [%s])\n' "$during" "$waited" "$after"; status=1
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    timeout -k 5 240 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/book_gaps/core_test.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        printf 'FAIL valgrind (did not finish in 240 s)\n'; status=1
    elif [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; printf 'FAIL valgrind\n'; status=1
    else
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "run_book_gaps: all checks passed"
exit $status
