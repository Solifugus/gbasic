#!/bin/bash
# G-SQL FROM A PROGRAM: the `gsql` module (src/modules/gsql.c;
# docs/gsql_module_design.md), PHASE 1's FOURTH INCREMENT. `load gsql`, then
# gsql.connect / query / exec / begin / commit / rollback / close, each call one
# request to the database's owner, which the program starts as its child.
#
# SELF-CHECKING NOT GOLDEN: every answer in tests/gsql/module_test.bas is
# written there by hand, and the defects this module can have are plausible
# values -- an integer one short past 2^53, money without its currency, a
# result cut at the first batch.
#
# TIERS:
#   semantics   the fixture's main phase: values in both directions, exec's
#               answer shape, batches, transactions, refusals with their codes
#   lost        the owner is killed under a program using it: the request in
#               flight fails with LOST and nothing is retried, and the next one
#               reaches a new owner that recovered what was committed
#   valgrind    the main phase again, no invalid access, no definite leak
#   leftovers   no owner outlives the suite, read-only
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
    printf 'run_gsql_module: FAILED (./gbasic did not build)\n'
    exit 1
fi
printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf 'SKIP run_gsql_module (this build has no G-SQL: %s -- the module was NOT measured)\n' "$probe"
    exit 0
fi

export GBASIC_PATH=stdlib
FIXTURE=tests/gsql/module_test.bas

# the fixture's verdict: its last line, and a floor on how many checks ran, so
# a fixture that stopped early cannot pass by mismatching nothing
verdict() {
    local name=$1 file=$2 rc=$3 floor=$4
    grep MISMATCH "$file" | head -12
    local last n
    last=$(tail -1 "$file")
    n=$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')
    if [ "$rc" = 124 ] || [ "$rc" = 137 ]; then
        fail "$name: did not finish -- a hang, reported as one"
    elif [ "$rc" != 0 ]; then
        grep -m3 'runtime error' "$file"
        fail "$name: the fixture exited $rc (${last:-no verdict})"
    elif [ -z "$n" ]; then
        fail "$name: ${last:-no verdict}"
    elif [ "$n" -lt "$floor" ]; then
        fail "$name: only $n checks ran, under the floor of $floor"
    else
        pass "$name: $last"
    fi
}

printf 'TIER semantics: a program as a client, every answer written by hand\n'
mkdir -p "$work/main"
GSQL_PHASE=main GSQL_DIR="$work/main" timeout -k 5 120 ./gbasic "$FIXTURE" >"$work/main.out" 2>"$work/main.err"
verdict semantics "$work/main.out" $? 50
if [ -s "$work/main.err" ]; then
    sed 's/^/       /' "$work/main.err" | head -5
    fail "semantics: the fixture wrote to stderr"
fi

printf 'TIER lost: the owner is killed under a program that is using it\n'
mkdir -p "$work/lost"
GSQL_PHASE=lost GSQL_DIR="$work/lost" timeout -k 5 60 ./gbasic "$FIXTURE" >"$work/lost.out" 2>"$work/lost.err" &
prog=$!
for _ in $(seq 1 400); do
    [ -e "$work/lost/ready" ] && break
    sleep 0.05
done
owner=""
if [ -e "$work/lost/ready" ]; then
    owner=$(pgrep -f -- "--gsql-owner $work/lost/lost " | head -1)
fi
if [ -z "$owner" ]; then
    fail "lost: the fixture never said it was ready, or no owner was found to kill"
    kill -9 "$prog" 2>/dev/null
    wait "$prog" 2>/dev/null
else
    # SIGKILL, not SIGTERM: the owner gets no planned handoff, so the next
    # owner's recovery is the crash path
    kill -9 "$owner"
    # dead or a zombie: the owner is the program's child and stays a zombie
    # until the program reaps it, which it does only once it is told to go on,
    # so `kill -0` alone would wait for ever
    for _ in $(seq 1 250); do
        st=$(ps -o stat= -p "$owner" 2>/dev/null | tr -d ' ')
        case "$st" in ''|Z*) break ;; esac
        sleep 0.02
    done
    touch "$work/lost/killed"
    wait "$prog"
    verdict lost "$work/lost.out" $? 4
fi

printf 'TIER blocks: the `database` and `sql` blocks, every answer written by hand\n'
BLOCKS="$PWD/tests/gsql/blocks_test.bas"
mkdir -p "$work/blocks"
(cd "$work/blocks" && timeout -k 5 120 "$OLDPWD/gbasic" "$BLOCKS") >"$work/blocks.out" 2>"$work/blocks.err"
verdict blocks "$work/blocks.out" $? 45
if [ -s "$work/blocks.err" ]; then
    sed 's/^/       /' "$work/blocks.err" | head -5
    fail "blocks: the fixture wrote to stderr"
fi

# A loop over a block OWNS its cursor and pulls a batch at a time, and nothing
# in a program can see either: what is asserted is the requests the module
# SENDS (GBASIC_GSQL_TRACE), between markers the program writes to the same
# stream. The CONTROL is the full iteration, which must fetch every batch and
# close nothing, or "a broken loop sends CLOSE" would be satisfied by a loop
# that always did.
printf 'TIER cursor: a loop that stops early closes its cursor and fetches nothing more\n'
mkdir -p "$work/cursor"
cat > "$work/cursor/c.bas" <<'GB'
load gsql
database t( file: "t.gsql", create: true )
sql mk on t
    create table n (k integer primary key)
end sql
sql ins on t
    insert into n (k) values (:k)
end sql
sql all_rows on t
    select k from n order by k
end sql
function boom()
    for each row in all_rows
        x = row.k + not_a_variable
    next
    return nothing
end function
function caught()
    on error goto next
    r = boom()
    e = error
    return e
end function
gsql.exec(mk)
gsql.begin(t)
for i = 1 to 1200
    k = i
    gsql.exec(ins)
next
gsql.commit(t)
print to error "MARK full"
c = 0
for each row in all_rows
    c = c + 1
next
print to error "MARK break"
for each row in all_rows
    break
next
print to error "MARK raise"
e = caught()
print to error "MARK return"
print string(c)
GB
(cd "$work/cursor" && GBASIC_GSQL_TRACE=1 timeout -k 5 60 "$OLDPWD/gbasic" c.bas) >"$work/cursor.out" 2>"$work/cursor.err"
crc=$?
between() {
    awk -v from="MARK $1" -v to="MARK $2" '$0 == from {on = 1; next} $0 == to {on = 0} on {sub(/^gsql-trace: /, ""); printf "%s ", $0}' "$work/cursor.err" | sed 's/ $//'
}
if [ "$crc" != 0 ] || [ "$(cat "$work/cursor.out")" != 1200 ]; then
    head -5 "$work/cursor.err"
    fail "cursor: the program did not run to its end (exit $crc)"
else
    full=$(between full break)
    brk=$(between break raise)
    rse=$(between raise return)
    if [ "$full" = "QUERY FETCH FETCH" ]; then
        pass "cursor: 1,200 rows are one QUERY and two FETCHes, and nothing to close: $full"
    else
        fail "cursor: the full iteration sent \"$full\", want \"QUERY FETCH FETCH\""
    fi
    if [ "$brk" = "QUERY CLOSE" ]; then
        pass "cursor: a break after one row fetches nothing more and closes: $brk"
    else
        fail "cursor: a break after one row sent \"$brk\", want \"QUERY CLOSE\""
    fi
    if [ "$rse" = "QUERY CLOSE" ]; then
        pass "cursor: a raise out of the loop body closes: $rse"
    else
        fail "cursor: a raise out of the loop body sent \"$rse\", want \"QUERY CLOSE\""
    fi
fi

# Everything decidable without a database is refused when the program LOADS,
# located in the file, with nothing run -- the server block's treatment. Each
# case: the source, the line:col it must name, and a phrase of its cause.
printf 'TIER load: refusals located in the file, before anything runs\n'
mkdir -p "$work/load"
load_case() {
    local name=$1 where=$2 phrase=$3 src=$4
    printf '%s\n' "$src" > "$work/load/$name.bas"
    local out err rc
    out=$(cd "$work/load" && "$OLDPWD/gbasic" "$name.bas" 2>"$work/load/$name.err")
    rc=$?
    err=$(cat "$work/load/$name.err")
    if [ "$rc" = 0 ]; then
        fail "load $name: the program was not refused"
    elif [ -n "$out" ]; then
        fail "load $name: something ran before the refusal: $out"
    elif ! printf '%s' "$err" | grep -q "at $name.bas:$where: "; then
        fail "load $name: not located at $where: $err"
    elif ! printf '%s' "$err" | grep -qF -- "$phrase"; then
        fail "load $name: does not say \"$phrase\": $err"
    else
        pass "load $name: $where, \"$phrase\""
    fi
}
HEAD='load gsql
print "ran"
database db( file: "x.gsql", create: true )'
load_case syntax 6:26 "sql 'q':" "$HEAD
sql q on db
    select id
    from item where id = = 1
end sql"
load_case at_end 5:19 "the end of the statement" "$HEAD
sql q on db
    select id from
end sql"
load_case no_database 4:1 "there is no \`database elsewhere( file: ... )\`" "$HEAD
sql q on elsewhere
    select 1
end sql"
load_case positional 5:12 "\$1 has nothing to supply it" "$HEAD
sql q on db
    select \$1
end sql"
load_case unknown_option 1:1 "unknown option 'creat'" 'database db( file: "x.gsql", creat: true )
load gsql
print "ran"'
load_case computed_option 2:1 "option 'file' must be a string literal" 'load gsql
database db( file: "x" + ".gsql" )'
load_case no_file 2:1 'says which file it is with `file:`' 'load gsql
database db( create: true )'
load_case twice 4:1 "declared twice" "$HEAD
database db( file: \"y.gsql\" )"
load_case in_function 5:5 "cannot be declared inside a function" "$HEAD
function f()
    sql q on db
        select 1
    end sql
    return nothing
end function"
load_case two_statements 5:15 "sql 'q':" "$HEAD
sql q on db
    select 1; select 2
end sql"
# captured first: the program is REFUSED, so gbasic exits 1, and under
# pipefail a `gbasic | grep -q` pipeline answers 1 even when grep matches
json=$(./gbasic --json-diagnostics "$work/load/syntax.bas" 2>&1 >/dev/null)
if printf '%s' "$json" | grep -q '"code":"GB_DIAG_GSQL_BLOCK".*"start":{"line":6,"column":26}'; then
    pass "load: the refusal reaches --json-diagnostics with its code and place"
else
    fail "load: --json-diagnostics did not carry GB_DIAG_GSQL_BLOCK at 6:26"
fi

printf 'TIER need: a block needs `load gsql`, refused before anything runs\n'
printf 'print "ran"\ndatabase db( file: "x.gsql", create: true )\n' > "$work/load/noload.bas"
out=$(cd "$work/load" && "$OLDPWD/gbasic" noload.bas 2>"$work/load/noload.err"); rc=$?
if [ "$rc" != 0 ] && [ -z "$out" ] && grep -q 'at noload.bas:2:1: .*does not `load gsql`' "$work/load/noload.err"; then
    pass "need: located at the block, and nothing ran"
else
    fail "need: exit $rc, stdout \"$out\": $(cat "$work/load/noload.err")"
fi
first=$(./gbasic --add-loads "$work/load/noload.bas" 2>&1 | head -1)
if [ "$first" = "load gsql" ]; then
    pass "need: --add-loads adds it, which it could not if the load were refused at load time"
else
    fail "need: --add-loads began with \"$first\", want \"load gsql\""
fi

# NOTHING IS RESERVED: the heads fire only on their exact shapes, so the words
# stay ordinary names everywhere else.
printf 'TIER words: `database` and `sql` are still ordinary names\n'
cat > "$work/load/words.bas" <<'GB'
function sql(x)
    return x + 1
end function
database = 2
sql_on = 3
print string(sql(database)) + " " + string(sql_on)
r = { sql: "a", database: "b" }
print r.sql + r.database
GB
out=$(./gbasic "$work/load/words.bas" 2>&1)
if [ "$out" = "3 3
ab" ]; then
    pass "words: a function, a variable and a field named for them"
else
    fail "words: $out"
fi

printf 'TIER hoist: blocks below `program` are bound before it runs\n'
mkdir -p "$work/hoist"
cat > "$work/hoist/h.bas" <<'GB'
load gsql
program main()
    gsql.exec(mk)
    v = 41
    gsql.exec(put)
    for each row in got
        print string(row.v + 1)
    next
end program
database d( file: "h.gsql", create: true )
sql mk on d
    create table t (v integer)
end sql
sql put on d
    insert into t (v) values (:v)
end sql
sql got on d
    select v from t
end sql
GB
out=$(cd "$work/hoist" && "$OLDPWD/gbasic" h.bas 2>&1)
if [ "$out" = 42 ]; then
    pass "hoist: declared below the block, used inside it"
else
    fail "hoist: $out"
fi

printf 'TIER unclosed: a block with no `end sql` names where it opened\n'
printf 'load gsql\ndatabase db( file: "x.gsql" )\nsql q on db\n    select 1\n' > "$work/load/open.bas"
err=$(./gbasic "$work/load/open.bas" 2>&1)
if printf '%s' "$err" | grep -qF "unclosed 'sql' block opened on line 3"; then
    pass "unclosed: names line 3, in the words the prompt reads as \"give me the rest\""
else
    fail "unclosed: $err"
fi

printf 'TIER valgrind: the main phase, no invalid access, no definite leak\n'
if vg_available; then
    mkdir -p "$work/vg"
    GSQL_PHASE=main GSQL_DIR="$work/vg" vg_run ./gbasic "$FIXTURE" >"$work/vg.out" 2>"$work/vg.err"
    vgst=$?
    # compared against $VG_EXIT: the fixture failing and valgrind objecting
    # are different findings (run_repl.sh's lesson)
    if [ "$vgst" = 0 ] && tail -1 "$work/vg.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: clean over $(tail -1 "$work/vg.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
        fail "valgrind objected"
    else
        tail -3 "$work/vg.out"
        fail "valgrind: the fixture did not pass under valgrind (exit $vgst; its mismatches are above)"
    fi
    mkdir -p "$work/vgb"
    (cd "$work/vgb" && vg_run "$OLDPWD/gbasic" "$BLOCKS") >"$work/vgb.out" 2>"$work/vgb.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vgb.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: the blocks, clean over $(tail -1 "$work/vgb.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vgb.err"
        fail "valgrind objected to the blocks"
    else
        tail -3 "$work/vgb.out"
        fail "valgrind: the blocks fixture did not pass under valgrind (exit $vgst)"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

printf 'TIER leftovers: no owner outlives the program that started it\n'
# the owner is the program's child and is ended with it (SIGTERM, then a
# planned handoff), which takes a moment
left=""
for _ in $(seq 1 100); do
    left=$(pgrep -f -- "--gsql-owner $work/" | tr '\n' ' ')
    [ -z "$left" ] && break
    sleep 0.1
done
if [ -z "$left" ]; then
    pass "no owner process is left"
else
    fail "owner processes outlived their programs: $left"
fi

if [ "$status" = 0 ]; then
    printf 'run_gsql_module: all checks passed\n'
else
    printf 'run_gsql_module: FAILED\n'
fi
exit "$status"
