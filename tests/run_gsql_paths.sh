#!/bin/bash
# G-SQL PATHS (docs/gsql_paths_design.md), increments 1 to 4: tables read by
# path -- `shop.customer[3]`, `shop.customer[.city = "Rome"]`, a result run when
# it is used, `count()` and `sql()` -- STEPS between them across foreign keys,
# `shop.customer[3].invoice`, `shop.invoice[11].customer`, and WRITES:
# `shop.customer[3] = { email: v }`, `.insert(...)`, `.delete()` -- and the
# columns a path reads in BRACES, `shop.customer[3]{ name }.purchases{ item }`. A path compiles in the owner to one SQL
# statement (src/gsql/sql_path.c) and runs by the protocol every statement
# does, so the oracle for what it answers is the SQL it means.
#
# TIERS:
#   grammar       the two forms the grammar gained, `.column` and a list in
#                 brackets, in --ast; and each refused outside a path
#   semantics     tests/gsql/paths_test.bas: keys, filters, three-valued
#                 unknown, primary key order, a result run when used, a path in
#                 a transaction, values never becoming SQL, sql()'s text, and
#                 every refusal beside the path that is allowed; every answer
#                 by hand
#   steps         tests/gsql/paths_steps_test.bas: steps down (many) and up
#                 (one from one), keys and filters on a step, a two-column
#                 foreign key, the refusals (two keys, a table joined to itself,
#                 none, a misspelling), and keys this connection holds going
#                 stale under a second connection: a round trip, never a wrong
#                 answer (§8.2); every answer by hand
#   writes        tests/gsql/paths_writes_test.bas: insert (all or none,
#                 defaults, through a step), a record assigned to a key (a
#                 missing one inserted by INSERT's rules), to a filter and
#                 through steps, delete with its cascade, a transaction, keys
#                 held going stale under a write; each read back by SQL
#   braces        tests/gsql/paths_braces_test.bas: braces on one table, a join
#                 from braces before a step (inner, one row per row reached), the
#                 refusals, and the forms `{` already had -- modifier clause,
#                 inline modifier, record literal -- unchanged; by hand
#   names         tests/gsql/paths_name_test.bas: `db["name"]`, the table a
#                 text names (literal, computed, passed to a function; a name an
#                 attribute takes, a reserved word, one with a space), keys,
#                 filters, braces straight after it, steps and writes through
#                 it, and the refusals (a number, a filter, two names, empty,
#                 a NUL, nothing, unknown, a record); by hand, writes read back
#   differential  random tables, rows and paths (tests/gsql/gen_paths_diff.py):
#                 each path's rows against the SQL the GENERATOR writes for it
#                 from its own model of the rules, never from sql(); a floor on
#                 the cases answering rows, so an empty tier cannot pass. Skips
#                 without python3
#   stepdiff      random chains of tables and paths walking up and down them,
#                 some with braces, against nested IN subqueries and comma
#                 joins the generator writes from its own model of one and many
#                 -- different SQL forms from the compiler's scalar, EXISTS and
#                 JOIN ... ON
#   writediff     two databases made alike, random writes by path to one and
#                 by the generator's own SQL to the other; after each, every
#                 table equal row for row and the same refusal on both sides,
#                 with a floor on writes that changed data
#   valgrind      the fixtures, no invalid access, no definite leak
#   leftovers     no owner outlives the suite
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/valgrind_tier.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

work="$(mktemp -d)"
cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

if ! make -s gbasic >"$work/make.out" 2>&1; then
    tail -20 "$work/make.out"
    printf 'run_gsql_paths: FAILED (./gbasic did not build)\n'
    exit 1
fi
printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf 'SKIP run_gsql_paths (this build has no G-SQL: %s -- paths were NOT measured)\n' "$probe"
    exit 0
fi
export GBASIC_PATH=stdlib

# the fixture's verdict: its last line, and a floor on how many checks ran
verdict() {
    local name=$1 file=$2 rc=$3 floor=$4
    grep MISMATCH "$file" | head -12
    local last n
    last=$(tail -1 "$file")
    n=$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        fail "$name: did not finish -- a hang, reported as one"
    elif [ "$rc" != 0 ]; then
        grep -m3 'runtime error\|syntax error' "$file"
        fail "$name: the fixture exited $rc (${last:-no verdict})"
    elif [ -z "$n" ]; then
        fail "$name: ${last:-no verdict}"
    elif [ "$n" -lt "$floor" ]; then
        fail "$name: only $n checks ran, under the floor of $floor"
    else
        pass "$name: $last"
    fi
}

printf 'TIER grammar: `.column` and a list in brackets, and each refused outside a path\n'
printf 'x = db.t[.a = 1, 2]\n' > "$work/ast.bas"
ast=$(./gbasic --ast "$work/ast.bas" 2>&1)
if printf '%s\n' "$ast" | grep -q 'Column a' && printf '%s\n' "$ast" | grep -q 'Subscripts'; then
    pass "grammar: --ast shows the column and the list"
else
    printf '%s\n' "$ast" | head -20
    fail "grammar: --ast did not show Column and Subscripts"
fi
printf 'x = .a\n' > "$work/col.bas"
out=$(./gbasic "$work/col.bas" 2>&1)
case "$out" in
    *'`.a` names a row'"'"'s field, and is read only inside brackets that choose rows'*) pass "grammar: a column outside a path is refused, saying where it belongs" ;;
    *) printf '       %s\n' "$out"; fail "grammar: a column outside a path" ;;
esac
printf 'a = [1, 2]\nprint a[0, 1]\n' > "$work/list.bas"
out=$(./gbasic "$work/list.bas" 2>&1)
case "$out" in
    *'an array or a record takes one index'*) pass "grammar: a list in an array's brackets is refused" ;;
    *) printf '       %s\n' "$out"; fail "grammar: a list in an array's brackets" ;;
esac
printf 'a = [10, 20]\nprint a[1]\nprint (a)[0]\n' > "$work/plain.bas"
out=$(./gbasic "$work/plain.bas" 2>&1)
if [ "$out" = "$(printf '20\n10')" ]; then
    pass "grammar: an array's one index is unchanged, parenthesized too"
else
    printf '       %s\n' "$out"
    fail "grammar: an array's one index changed"
fi

printf 'TIER semantics: one table by path, every answer by hand\n'
mkdir -p "$work/main"
GSQL_DIR="$work/main" timeout -k 5 120 ./gbasic tests/gsql/paths_test.bas >"$work/main.out" 2>"$work/main.err"
verdict semantics "$work/main.out" $? 90
if [ -s "$work/main.err" ]; then
    sed 's/^/       /' "$work/main.err" | head -5
    fail "semantics: the fixture wrote to stderr"
fi

printf 'TIER steps: across foreign keys, every answer by hand\n'
mkdir -p "$work/steps"
GSQL_DIR="$work/steps" timeout -k 5 120 ./gbasic tests/gsql/paths_steps_test.bas >"$work/steps.out" 2>"$work/steps.err"
verdict steps "$work/steps.out" $? 35
if [ -s "$work/steps.err" ]; then
    sed 's/^/       /' "$work/steps.err" | head -5
    fail "steps: the fixture wrote to stderr"
fi

printf 'TIER writes: by path, every answer by hand and read back by SQL\n'
mkdir -p "$work/writes"
GSQL_DIR="$work/writes" timeout -k 5 120 ./gbasic tests/gsql/paths_writes_test.bas >"$work/writes.out" 2>"$work/writes.err"
verdict writes "$work/writes.out" $? 55
if [ -s "$work/writes.err" ]; then
    sed 's/^/       /' "$work/writes.err" | head -5
    fail "writes: the fixture wrote to stderr"
fi

printf 'TIER braces: the columns a path reads, and joins, every answer by hand\n'
mkdir -p "$work/braces"
GSQL_DIR="$work/braces" timeout -k 5 120 ./gbasic tests/gsql/paths_braces_test.bas >"$work/braces.out" 2>"$work/braces.err"
verdict braces "$work/braces.out" $? 30
if [ -s "$work/braces.err" ]; then
    sed 's/^/       /' "$work/braces.err" | head -5
    fail "braces: the fixture wrote to stderr"
fi

printf 'TIER names: db["name"], the table a text names, every answer by hand\n'
mkdir -p "$work/names"
GSQL_DIR="$work/names" timeout -k 5 120 ./gbasic tests/gsql/paths_name_test.bas >"$work/names.out" 2>"$work/names.err"
verdict names "$work/names.out" $? 44
if [ -s "$work/names.err" ]; then
    sed 's/^/       /' "$work/names.err" | head -5
    fail "names: the fixture wrote to stderr"
fi

printf 'TIER differential: random paths against the SQL a model of the rules writes for them\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP (no python3 -- the differential tier was NOT measured)\n'
else
    for seed in 1 2 3 4 5; do
        mkdir -p "$work/diff$seed"
        if ! python3 tests/gsql/gen_paths_diff.py "$seed" "$work/diff$seed/db" > "$work/diff$seed.bas"; then
            fail "differential seed $seed: the generator failed"
            continue
        fi
        timeout -k 5 300 ./gbasic "$work/diff$seed.bas" >"$work/diff$seed.out" 2>"$work/diff$seed.err"
        rc=$?
        verdict "differential seed $seed" "$work/diff$seed.out" $rc 120
        rows=$(sed -n 's/^answered \([0-9]*\) of .*/\1/p' "$work/diff$seed.out")
        if [ "$rc" = 0 ] && [ "${rows:-0}" -lt 30 ]; then
            fail "differential seed $seed: only ${rows:-0} cases answered rows, under the floor of 30"
        fi
        if [ -s "$work/diff$seed.err" ]; then
            sed 's/^/       /' "$work/diff$seed.err" | head -5
            fail "differential seed $seed: the program wrote to stderr"
        fi
    done
fi

printf 'TIER stepdiff: random walks along a chain of tables, against nested IN subqueries\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP (no python3 -- the stepdiff tier was NOT measured)\n'
else
    for seed in 1 2 3 4 5; do
        mkdir -p "$work/sdiff$seed"
        if ! python3 tests/gsql/gen_paths_diff.py "$seed" "$work/sdiff$seed/db" steps > "$work/sdiff$seed.bas"; then
            fail "stepdiff seed $seed: the generator failed"
            continue
        fi
        timeout -k 5 300 ./gbasic "$work/sdiff$seed.bas" >"$work/sdiff$seed.out" 2>"$work/sdiff$seed.err"
        rc=$?
        verdict "stepdiff seed $seed" "$work/sdiff$seed.out" $rc 100
        rows=$(sed -n 's/^answered \([0-9]*\) of .*/\1/p' "$work/sdiff$seed.out")
        if [ "$rc" = 0 ] && [ "${rows:-0}" -lt 15 ]; then
            fail "stepdiff seed $seed: only ${rows:-0} cases answered rows, under the floor of 15"
        fi
        if [ -s "$work/sdiff$seed.err" ]; then
            sed 's/^/       /' "$work/sdiff$seed.err" | head -5
            fail "stepdiff seed $seed: the program wrote to stderr"
        fi
    done
fi

printf 'TIER writediff: random writes by path and by SQL to two databases made alike\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP (no python3 -- the writediff tier was NOT measured)\n'
else
    for seed in 1 2 3 4 5; do
        mkdir -p "$work/wdiff$seed"
        if ! python3 tests/gsql/gen_paths_diff.py "$seed" "$work/wdiff$seed/db" writes > "$work/wdiff$seed.bas"; then
            fail "writediff seed $seed: the generator failed"
            continue
        fi
        timeout -k 5 300 ./gbasic "$work/wdiff$seed.bas" >"$work/wdiff$seed.out" 2>"$work/wdiff$seed.err"
        rc=$?
        verdict "writediff seed $seed" "$work/wdiff$seed.out" $rc 60
        changed=$(sed -n 's/^changed the data \([0-9]*\) of .*/\1/p' "$work/wdiff$seed.out")
        if [ "$rc" = 0 ] && [ "${changed:-0}" -lt 15 ]; then
            fail "writediff seed $seed: only ${changed:-0} writes changed data, under the floor of 15"
        fi
        if [ -s "$work/wdiff$seed.err" ]; then
            sed 's/^/       /' "$work/wdiff$seed.err" | head -5
            fail "writediff seed $seed: the program wrote to stderr"
        fi
    done
fi

vg_program_stderr() { grep -v '^==[0-9]*==' "$1" | tail -5 | sed 's/^/       stderr: /'; }
printf 'TIER valgrind: the semantics fixture, no invalid access, no definite leak\n'
if vg_available; then
    mkdir -p "$work/vg"
    GSQL_DIR="$work/vg" vg_run ./gbasic tests/gsql/paths_test.bas >"$work/vg.out" 2>"$work/vg.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vg.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: clean over $(tail -1 "$work/vg.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
        fail "valgrind objected"
    else
        tail -3 "$work/vg.out"
        vg_program_stderr "$work/vg.err"
        fail "valgrind: the fixture did not pass under valgrind (exit $vgst)"
    fi
    mkdir -p "$work/vgb"
    GSQL_DIR="$work/vgb" vg_run ./gbasic tests/gsql/paths_braces_test.bas >"$work/vgb.out" 2>"$work/vgb.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vgb.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: braces clean over $(tail -1 "$work/vgb.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vgb.err"
        fail "valgrind objected to the braces fixture"
    else
        tail -3 "$work/vgb.out"
        vg_program_stderr "$work/vgb.err"
        fail "valgrind: the braces fixture did not pass under valgrind (exit $vgst)"
    fi
    mkdir -p "$work/vgw"
    GSQL_DIR="$work/vgw" vg_run ./gbasic tests/gsql/paths_writes_test.bas >"$work/vgw.out" 2>"$work/vgw.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vgw.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: writes clean over $(tail -1 "$work/vgw.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vgw.err"
        fail "valgrind objected to the writes fixture"
    else
        tail -3 "$work/vgw.out"
        vg_program_stderr "$work/vgw.err"
        fail "valgrind: the writes fixture did not pass under valgrind (exit $vgst)"
    fi
    mkdir -p "$work/vgs"
    GSQL_DIR="$work/vgs" vg_run ./gbasic tests/gsql/paths_steps_test.bas >"$work/vgs.out" 2>"$work/vgs.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vgs.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: steps clean over $(tail -1 "$work/vgs.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vgs.err"
        fail "valgrind objected to the steps fixture"
    else
        tail -3 "$work/vgs.out"
        vg_program_stderr "$work/vgs.err"
        fail "valgrind: the steps fixture did not pass under valgrind (exit $vgst)"
    fi
    mkdir -p "$work/vgn"
    GSQL_DIR="$work/vgn" vg_run ./gbasic tests/gsql/paths_name_test.bas >"$work/vgn.out" 2>"$work/vgn.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vgn.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: names clean over $(tail -1 "$work/vgn.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vgn.err"
        fail "valgrind objected to the names fixture"
    else
        tail -3 "$work/vgn.out"
        vg_program_stderr "$work/vgn.err"
        fail "valgrind: the names fixture did not pass under valgrind (exit $vgst)"
    fi
else
    printf '  SKIP (no valgrind -- memory was NOT measured)\n'
fi

printf 'TIER leftovers: no owner outlives the suite\n'
sleep 1
if pgrep -f -- "--gsql-owner $work" >/dev/null 2>&1; then
    pgrep -af -- "--gsql-owner $work" | head -3
    fail "leftovers: an owner of this suite's databases is still running"
else
    pass "leftovers: none"
fi

if [ "$status" = 0 ]; then
    printf 'PASS run_gsql_paths\n'
else
    printf 'FAIL run_gsql_paths\n'
fi
exit "$status"
