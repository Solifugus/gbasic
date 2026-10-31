#!/bin/bash
# G-SQL's SQL LAYER (include/gsql_sql.h; src/gsql/sql_parse.c, sql_plan.c,
# sql_exec.c; src/modules/gsql.c; docs/gsql_sql_design.md), PHASE 1's THIRD
# INCREMENT: parser, planner, executor and EXPLAIN, run as the owner's service,
# and the increment that links G-SQL into ./gbasic (`gbasic --gsql-owner`).
#
# EVERY OPERATOR IS gBASIC'S: the host evaluates `=`, `<`, `+`, `and` as
# two-operand gBASIC templates parsed by gBASIC's parser, so the semantics
# tier links libgbasic.a and runs against the interpreter itself. What is
# SQL's own (IS NULL, ||, IN, BETWEEN, LIKE, CASE, COALESCE, NULLIF, WHERE
# keeping only true, no implicit casts) is the SQL layer's, in C.
#
# TIERS:
#   parse        a corpus printed back in canonical form, and refusals each with
#                its position; no store, no interpreter
#   semantics    statements in process against the interpreter's host, every
#                answer written by hand (tests/gsql/gen_sql_cases.py), each
#                refusal pinned to a phrase of its cause
#   valgrind     the semantics tier again, no invalid access, no definite leak
#   differential random rows and predicates, each query run with access paths
#                and forced to a scan: identical answers, with a census floor
#                so a planner that never seeks cannot pass by agreeing
#   owner        `gbasic --gsql-owner` started by the client, end to end
#   leftovers    no owner outlives the suite, read-only
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
rt="$(mktemp -d /tmp/gsql-rt.XXXXXX)"
chmod 700 "$rt"
cleanup() {
    rm -rf "$work" "$rt"
}
trap cleanup EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

CC=${CC:-cc}
if ! command -v "$CC" >/dev/null 2>&1; then
    printf 'SKIP run_gsql_sql (no C compiler -- the SQL layer was NOT measured)\n'
    exit 0
fi
FLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc/gsql"
ENGINE="src/gsql/lsm.c src/gsql/store_common.c src/gsql/store_util.c src/gsql/io.c"
LAYER="src/gsql/v3.c src/gsql/table.c src/gsql/key.c src/gsql/wire.c"
SAN=""
printf 'int main(void){return 0;}\n' > "$work/probe.c"
if "$CC" -fsanitize=address,undefined -o "$work/probe" "$work/probe.c" 2>/dev/null; then
    SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
fi
bounded() { timeout -k 5 "$1" "${@:2}"; }

# one test program's verdict line: the last line, "ok N checks, 0 mismatches"
verdict() {
    local name=$1 file=$2 rc=$3
    local last
    last=$(tail -1 "$file")
    grep MISMATCH "$file" | head -12
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        fail "$name: did not finish -- a hang, reported as one"
        return
    fi
    if grep -q 'runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer' "$file"; then
        grep -m3 'runtime error\|ERROR: AddressSanitizer\|ERROR: LeakSanitizer' "$file"
        fail "$name: the sanitizers objected"
        return
    fi
    case "$last" in
        "ok "*" 0 mismatches") pass "$name: $last" ;;
        *) fail "$name: ${last:-no verdict (exit $rc)}" ;;
    esac
}

printf 'TIER build: ./gbasic with G-SQL linked, and the test programs, -Wpedantic -Werror\n'
if ! make -s gbasic libgbasic.a >"$work/make.out" 2>&1; then
    tail -20 "$work/make.out"
    fail "./gbasic did not build"
    printf 'run_gsql_sql: FAILED (nothing below was run)\n'
    exit 1
fi
LIBS=$(make -s print-ldlibs)
# shellcheck disable=SC2086
if "$CC" $FLAGS -g -O1 $SAN -o "$work/parse_test" tests/gsql/sql_parse_test.c src/gsql/sql_parse.c $LAYER $ENGINE \
        2>"$work/build.err" &&
   "$CC" $FLAGS -g -o "$work/sql_test" tests/gsql/sql_test.c libgbasic.a $LIBS 2>>"$work/build.err" &&
   "$CC" $FLAGS -g -O1 $SAN -D_POSIX_C_SOURCE=200809L -o "$work/owner_test" tests/gsql/sql_owner_test.c \
        src/gsql/client.c src/gsql/owner.c src/platform_posix.c $LAYER $ENGINE 2>>"$work/build.err"; then
    pass "parse_test${SAN:+ (under ASan and UBSan)}, sql_test (linked to libgbasic.a) and owner_test"
else
    head -12 "$work/build.err"
    fail "the test programs did not compile cleanly"
    printf 'run_gsql_sql: FAILED (nothing below was run)\n'
    exit 1
fi
if ! grep -q 'gb_gsql_owner_main' <(nm -C ./gbasic 2>/dev/null); then
    fail "./gbasic has no gb_gsql_owner_main: G-SQL is not linked into the binary"
fi

printf 'TIER parse: the corpus prints back canonically; refusals name their position\n'
bounded 60 "$work/parse_test" tests/gsql/sql_parse_cases.txt >"$work/parse.out" 2>&1
verdict parse "$work/parse.out" $?

printf 'TIER semantics: every answer written by hand, every operator gBASIC'"'"'s\n'
# the committed corpus must be what its generator writes, or the answers in it
# are no one's
if command -v python3 >/dev/null 2>&1; then
    python3 tests/gsql/gen_sql_cases.py "$work/regen.txt"
    if cmp -s "$work/regen.txt" tests/gsql/sql_cases.txt; then
        pass "tests/gsql/sql_cases.txt is what tests/gsql/gen_sql_cases.py writes"
    else
        fail "tests/gsql/sql_cases.txt differs from what tests/gsql/gen_sql_cases.py writes: regenerate it"
    fi
fi
mkdir -p "$work/sem"
bounded 120 "$work/sql_test" script tests/gsql/sql_cases.txt "$work/sem/db" >"$work/sem.out" 2>"$work/sem.err"
verdict semantics "$work/sem.out" $?
# a warning from inside SQL names the SQL, never the host's template
if grep -q '^warning: .*, in a G-SQL expression \[2112\]$' "$work/sem.err" && ! grep -q '?:1:' "$work/sem.err"; then
    pass "an overflow warning says it came from a G-SQL expression, and names no template position"
else
    sed 's/^/       /' "$work/sem.err" | head -5
    fail "the overflow warning is missing, or names a position in the host's template"
fi

printf 'TIER valgrind: the semantics corpus, no invalid access, no definite leak\n'
if vg_available; then
    mkdir -p "$work/vg"
    vg_run "$work/sql_test" script tests/gsql/sql_cases.txt "$work/vg/db" >"$work/vg.out" 2>"$work/vg.err"
    vgst=$?
    # compared against $VG_EXIT rather than "nonzero": the program failing and
    # valgrind objecting are different findings (run_repl.sh's lesson)
    if [ "$vgst" = 0 ] && tail -1 "$work/vg.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: clean over $(tail -1 "$work/vg.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
        fail "valgrind objected"
    else
        tail -3 "$work/vg.out"
        fail "valgrind: the program itself failed under valgrind (exit $vgst)"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

printf 'TIER differential: access paths against a forced scan, five seeds\n'
for seed in 1 2 3 4 5; do
    rm -rf "$work/diff"
    mkdir -p "$work/diff"
    bounded 300 "$work/sql_test" diff "$work/diff/db" "$seed" 600 >"$work/diff$seed.out" 2>&1
    rc=$?
    grep '^census' "$work/diff$seed.out" | sed 's/^/       seed '"$seed"' /'
    verdict "differential seed $seed" "$work/diff$seed.out" $rc
done

printf 'TIER hash cost: an unindexed join hashed against its nested loop, as a ratio\n'
rm -rf "$work/hc"
mkdir -p "$work/hc"
bounded 300 "$work/sql_test" hashcost "$work/hc/db" 1000 >"$work/hc.out" 2>&1
rc=$?
grep '^hash ' "$work/hc.out" | sed 's/^/       /'
verdict "hash cost" "$work/hc.out" $rc

printf 'TIER owner: gbasic --gsql-owner, started by a client, end to end\n'
mkdir -p "$work/own"
XDG_RUNTIME_DIR="$rt" bounded 120 "$work/owner_test" ./gbasic "$work/own" >"$work/own.out" 2>&1
verdict owner "$work/own.out" $?

printf 'TIER leftovers: no owner outlives the suite\n'
sleep 0.3
left=$(pgrep -f -- "--gsql-owner $work/" | tr '\n' ' ')
if [ -z "$left" ]; then
    pass "no owner process is left"
else
    fail "owner processes outlived their clients: $left"
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_sql: all checks passed\n'
else
    printf 'run_gsql_sql: FAILED\n'
fi
exit "$status"
