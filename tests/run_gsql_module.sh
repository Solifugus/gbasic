#!/bin/bash
# G-SQL FROM A PROGRAM: the `gsql` module (src/modules/gsql.c;
# docs/gsql_module_design.md), PHASE 1's FOURTH INCREMENT. `load gsql`, then
# gsql.connect / query / exec / begin / commit / rollback / close, each call one
# request to the database's owner, which the program starts and which stops
# by itself once nobody has used it for its linger.
#
# SELF-CHECKING NOT GOLDEN: every answer in tests/gsql/module_test.bas is
# written there by hand, and the defects this module can have are plausible
# values -- an integer one short past 2^53, money without its currency, a
# result cut at the first batch.
#
# TIERS:
#   semantics   the fixture's main phase: values in both directions, exec's
#               answer shape, batches, transactions, refusals with their codes
#   backup      gsql.backup: a first copy and a refresh that keeps files, the
#               copy read by a program of its own, and each refusal (a copy in
#               use, the live database, a directory of other files)
#   random      RANDOM's draws are fair: with a 200-character alphabet a plain
#               "byte mod 200" would draw the first 56 at 0.44, not 0.28
#   lost        the owner is killed under a program using it: the request in
#               flight fails with LOST and nothing is retried, and the next one
#               reaches a new owner that recovered what was committed
#   lifetime    an owner outlives the program that started it while another
#               program uses it, then stops after its linger; linger: 0; the
#               option's refusals; no program's output held open
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

printf 'TIER random: RANDOM draws every value equally often (docs/gsql_generated_design.md §3)\n'
mkdir -p "$work/random"
GSQL_DIR="$work/random" timeout -k 5 120 ./gbasic tests/gsql/random_test.bas >"$work/random.out" 2>"$work/random.err"
verdict random "$work/random.out" $? 6
if [ -s "$work/random.err" ]; then
    sed 's/^/       /' "$work/random.err" | head -5
    fail "random: the fixture wrote to stderr"
fi

printf 'TIER max_recursion: the WITH RECURSIVE round limit of a database, set from the program\n'
# each limit asserted as a difference: the same statement answering under one
# setting and refused under another (docs/gsql_recursive_design.md §3)
mkdir -p "$work/maxrec"
(cd "$work/maxrec" && timeout -k 5 120 "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/max_recursion_test.bas") \
    >"$work/maxrec.out" 2>"$work/maxrec.err"
verdict max_recursion "$work/maxrec.out" $? 15
if [ -s "$work/maxrec.err" ]; then
    sed 's/^/       /' "$work/maxrec.err" | head -5
    fail "max_recursion: the fixture wrote to stderr"
fi

printf 'TIER integer text: text that is exactly an integer is stored as one, and nothing else\n'
# Matthew's ruling 2026-11-02 (gbasic-books volume 3, Q1); every shape's answer
# by hand, each refused one beside an accepted neighbour
mkdir -p "$work/inttext"
(cd "$work/inttext" && timeout -k 5 120 "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/integer_text_test.bas") \
    >"$work/inttext.out" 2>"$work/inttext.err"
verdict "integer text" "$work/inttext.out" $? 45
if [ -s "$work/inttext.err" ]; then
    sed 's/^/       /' "$work/inttext.err" | head -5
    fail "integer text: the fixture wrote to stderr"
fi

printf 'TIER compare text: a comparison reads text as the other side'"'"'s kind, and a seek agrees with a scan\n'
# gdash S3/S4, Matthew's ruling 2026-11-02 (docs/gsql_compare_conversion_design.md)
mkdir -p "$work/cmptext"
(cd "$work/cmptext" && timeout -k 5 120 "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/compare_text_test.bas") \
    >"$work/cmptext.out" 2>"$work/cmptext.err"
verdict "compare text" "$work/cmptext.out" $? 49
if [ -s "$work/cmptext.err" ]; then
    sed 's/^/       /' "$work/cmptext.err" | head -5
    fail "compare text: the fixture wrote to stderr"
fi

printf 'TIER details: a refusal names its table, column and constraint beside its code\n'
# gbasic-books volume 3, G2: the names were only inside the message, so a reject
# report routing by reason had to parse English (docs/gsql_error_details_design.md)
mkdir -p "$work/details"
(cd "$work/details" && timeout -k 5 120 "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/error_details_test.bas") \
    >"$work/details.out" 2>"$work/details.err"
verdict details "$work/details.out" $? 12
if [ -s "$work/details.err" ]; then
    sed 's/^/       /' "$work/details.err" | head -5
    fail "details: the fixture wrote to stderr"
fi

printf 'TIER library: `database` and `sql` blocks inside a library are the library'"'"'s own\n'
# gbasic-books volume 3, G3: declared in a loaded library they used to be
# accepted and bound nowhere, so every use failed three different ways, none
# naming the cause (docs/gsql_library_blocks_design.md)
mkdir -p "$work/libblocks"
(cd "$work/libblocks" && timeout -k 5 120 "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/libblocks/libblocks_test.bas") \
    >"$work/libblocks.out" 2>"$work/libblocks.err"
verdict library "$work/libblocks.out" $? 16
if [ -s "$work/libblocks.err" ]; then
    sed 's/^/       /' "$work/libblocks.err" | head -5
    fail "library: the fixture wrote to stderr"
fi
# a loaded library FILE is checked as the program's own file is, so its
# mistakes are refused at their line in it, before anything runs
lib_case() {
    local name=$1 where=$2 phrase=$3 body=$4
    local d="$work/libcase_$name"
    mkdir -p "$d"
    printf 'library %s\n    load gsql\n%s\nend library\n' "$name" "$body" > "$d/$name.bas"
    printf 'load %s\nprint "ran"\n' "$name" > "$d/main.bas"
    local out err rc
    out=$(cd "$d" && "$OLDPWD/gbasic" main.bas 2>"$d/err")
    rc=$?
    err=$(cat "$d/err")
    if [ "$rc" = 0 ]; then
        fail "library refusal $name: the program was not refused"
    elif [ -n "$out" ]; then
        fail "library refusal $name: something ran before the refusal: $out"
    elif ! printf '%s' "$err" | grep -q "$name.bas:$where: "; then
        fail "library refusal $name: not located at $name.bas:$where: $err"
    elif ! printf '%s' "$err" | grep -qF -- "$phrase"; then
        fail "library refusal $name: does not say \"$phrase\": $err"
    else
        pass "library refusal $name: at $name.bas:$where, \"$phrase\""
    fi
}
lib_case badsql 5:9 "sql 'oops': expected a statement" '    database shop( file: "x.gsql" )
    sql oops on shop
        selec 1
    end sql'
lib_case nodb 3:5 "there is no \`database elsewhere( file: ... )\` in this library" '    sql q on elsewhere
        select 1
    end sql'
lib_case nested 5:9 "a \`sql\` block cannot be declared inside a function" '    database shop( file: "x.gsql" )
    function f()
        sql q on shop
            select 1
        end sql
    end function'
lib_case notliteral 3:5 "database 'shop'" '    database shop( file: "x" + ".gsql" )'
lib_case twice 4:5 "\`shop\` is declared twice" '    database shop( file: "x.gsql" )
    database shop( file: "y.gsql" )'

printf 'TIER sweep: an owner starting removes a crashed process sorts and spools, and only those\n'
# a process gone (a child that exited) and one alive (this shell) each with a
# directory named for it, in a TMPDIR of their own; an owner the program
# starts must remove the first and keep the second (gs_xsort_sweep)
mkdir -p "$work/sweep/tmp"
sh -c 'exit 0' &
gone=$!
wait "$gone"
mkdir -p "$work/sweep/tmp/gsql-sort-$gone-abcdef" "$work/sweep/tmp/gsql-spool-$gone-abcdef" \
    "$work/sweep/tmp/gsql-sort-$$-abcdef"
printf 'x' >"$work/sweep/tmp/gsql-sort-$gone-abcdef/run-0"
cat >"$work/sweep/s.bas" <<'GB'
load gsql
database d( file: "s.gsql", create: true, linger: 0 )
print d.query("select 1 as one")[0].one
d.close()
GB
sout=$(cd "$work/sweep" && TMPDIR="$work/sweep/tmp" timeout -k 5 60 "$OLDPWD/gbasic" s.bas 2>&1)
if [ "$sout" = 1 ] && [ ! -e "$work/sweep/tmp/gsql-sort-$gone-abcdef" ] && \
   [ ! -e "$work/sweep/tmp/gsql-spool-$gone-abcdef" ] && [ -d "$work/sweep/tmp/gsql-sort-$$-abcdef" ]; then
    pass "sweep: the gone process's sort and spool were removed at the owner's start, a live one's kept"
else
    printf '       %s\n' "$sout" "$(ls "$work/sweep/tmp")"
    fail "sweep: the owner's start did not remove exactly the gone process's directories"
fi

printf 'TIER backup: gsql.backup, a copy made while the database is in use, and refreshed\n'
mkdir -p "$work/backup"
(cd "$work/backup" && GBASIC_BIN="$OLDPWD/gbasic" GBASIC_PATH="$OLDPWD/stdlib" timeout -k 5 300 \
    "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/backup_test.bas") >"$work/backup.out" 2>"$work/backup.err"
verdict backup "$work/backup.out" $? 15
if [ -s "$work/backup.err" ]; then
    sed 's/^/       /' "$work/backup.err" | head -5
    fail "backup: the fixture wrote to stderr"
fi

printf 'TIER dump: gsql.dump and gsql.restore, the restored values compared as serialize bytes\n'
# docs/gsql_dump_design.md §6: the oracle is the stored values, since a dump of
# a restore matching the dump it came from passes on a printer that loses digits
mkdir -p "$work/dump"
GSQL_DIR="$work/dump" timeout -k 5 300 ./gbasic tests/gsql/dump_test.bas >"$work/dump.out" 2>"$work/dump.err"
verdict dump "$work/dump.out" $? 70
if [ -s "$work/dump.err" ]; then
    sed 's/^/       /' "$work/dump.err" | head -5
    fail "dump: the fixture wrote to stderr"
fi

printf 'TIER warnings: a G-SQL warning is a gBASIC warning, at the program'"'"'s line\n'
WFIX=tests/gsql/warning_test.bas
mkdir -p "$work/warn"
GSQL_DIR="$work/warn" timeout -k 5 60 ./gbasic "$WFIX" >"$work/warn.out" 2>"$work/warn.err"
verdict warnings "$work/warn.out" $? 5
# each `' WARNS` line warns exactly once on stderr, naming its own line; each
# `' QUIET` line does not warn; and nothing else does
expect=0
while IFS=: read -r ln _; do
    expect=$((expect + 1))
    got=$(grep -c "in a G-SQL expression at $WFIX:$ln:[0-9]* \[2112\]\$" "$work/warn.err" || true)
    if [ "$got" = 1 ]; then
        pass "line $ln warns once, at its own line"
    else
        fail "line $ln should warn once at its own line, and warned $got times"
    fi
done < <(grep -nE "' WARNS( ONCE)?\$" "$WFIX")
while IFS=: read -r ln _; do
    if grep -q "at $WFIX:$ln:" "$work/warn.err"; then
        fail "line $ln should be quiet, and warned"
    else
        pass "line $ln is quiet"
    fi
done < <(grep -nE "' QUIET$" "$WFIX")
while IFS=: read -r ln _; do
    expect=$((expect + 2))
    got=$(grep "in a G-SQL expression at $WFIX:$ln:[0-9]* \[2112\]\$" "$work/warn.err" | sort -u | wc -l)
    if [ "$got" = 2 ]; then
        pass "line $ln warns twice, once for each different message"
    else
        fail "line $ln should warn once for each of two messages, and warned $got distinct times"
    fi
done < <(grep -nE "' WARNS TWICE\$" "$WFIX")
total=$(grep -c '^warning: ' "$work/warn.err" || true)
if [ "$expect" -ge 2 ] && [ "$total" = "$expect" ]; then
    pass "stderr holds exactly the $expect expected warnings"
else
    sed 's/^/       /' "$work/warn.err" | head -6
    fail "stderr holds $total warnings where $expect were expected"
fi

# the view the fixture made survives its program: a second one, a new owner
# over the same file, reads it (docs/gsql_view_design.md §4)
cat > "$work/warn/reopen.bas" <<'GB'
load gsql
db = gsql.connect(env("GSQL_DIR") + "/w.gsql")
print string(gsql.query(db, "select v from seen")[0].v)
GB
reopen=$(GSQL_DIR="$work/warn" timeout -k 5 60 ./gbasic "$work/warn/reopen.bas" 2>&1)
if [ "$reopen" = 10 ]; then
    pass "a view survives its program, read by the next one through a new owner"
else
    fail "a reopened database did not read its view: $reopen"
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
    # gone, or a zombie for the moment before init reaps it: the owner is
    # nobody's child (docs/gsql_owner_lifetime_design.md)
    for _ in $(seq 1 250); do
        st=$(ps -o stat= -p "$owner" 2>/dev/null | tr -d ' ')
        case "$st" in ''|Z*) break ;; esac
        sleep 0.02
    done
    touch "$work/lost/killed"
    wait "$prog"
    verdict lost "$work/lost.out" $? 4
fi

printf 'TIER lifetime: an owner outlives the program that started it while another uses it\n'
# docs/gsql_owner_lifetime_design.md. THE DEFECT, measured on the build
# before it: a second program connected to an owner the first one started
# lost its next request (LOST) the moment the first exited, because the owner
# was its starter's child and went with it.
LIFE="$PWD/tests/gsql/lifetime_test.bas"
L="$work/life"
mkdir -p "$L"
life_owner() {   # the owner serving database $1, or nothing
    pgrep -f -- "--gsql-owner (--linger=[0-9]+ )?$1 " | head -1
}
GSQL_ROLE=starter GSQL_DB="$L/shared" GSQL_GATE="$L/g1" timeout -k 5 60 ./gbasic "$LIFE" >"$L/a.out" 2>&1 &
a=$!
for _ in $(seq 1 200); do grep -q ready "$L/a.out" 2>/dev/null && break; sleep 0.05; done
GSQL_ROLE=other GSQL_DB="$L/shared" GSQL_GATE="$L/g2" timeout -k 5 60 ./gbasic "$LIFE" >"$L/b.out" 2>&1 &
b=$!
for _ in $(seq 1 200); do grep -q first "$L/b.out" 2>/dev/null && break; sleep 0.05; done
owner=$(life_owner "$L/shared")
touch "$L/g1"
wait "$a"
touch "$L/g2"
wait "$b"
bst=$?
if [ "$bst" = 0 ] && grep -qx "after the starter exited 2" "$L/b.out"; then
    pass "lifetime: the second program's connection survived its starter exiting"
else
    sed 's/^/       /' "$L/b.out" | head -5
    fail "lifetime: the second program lost its owner when the program that started it exited (exit $bst)"
fi
# the owner went once both had: after its linger (3 s by default), not with
# either program
if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then
    pass "lifetime: the owner is still there just after its last program went"
else
    fail "lifetime: the owner (${owner:-not found}) went with its last program, before its linger"
fi
gone=0
for _ in $(seq 1 80); do
    kill -0 "$owner" 2>/dev/null || { gone=1; break; }
    sleep 0.1
done
[ "$gone" = 1 ] && pass "lifetime: and gone once its linger had passed" \
    || fail "lifetime: the owner $owner was still there 8 s after its last program went"

# linger: 0, on connect and on a `database` block, stops at the last close
for role in quick block; do
    ( cd "$L" && GSQL_ROLE=$role GSQL_DB="$L/quick" timeout -k 5 60 "$OLDPWD/gbasic" "$LIFE" ) >"$L/$role.out" 2>&1
    db="$L/quick"
    [ "$role" = block ] && db="$L/lifetime-block.gsql"
    left=""
    for _ in $(seq 1 10); do
        left=$(life_owner "$db")
        [ -z "$left" ] && break
        sleep 0.1
    done
    if ! grep -qx "$role done" "$L/$role.out"; then
        sed 's/^/       /' "$L/$role.out" | head -3
        fail "lifetime: with linger: 0 through $role the program did not finish its work"
    elif [ -n "$left" ]; then
        fail "lifetime: with linger: 0 through $role the owner ($left) was still there a second later"
    else
        pass "lifetime: linger: 0 through $role stops the owner at the last close"
    fi
done

# an owner holds none of the program's streams, so capturing a program's
# output returns when the PROGRAM does, not after its owner's linger
t0=$(date +%s%N)
out=$(GSQL_ROLE=starter GSQL_DB="$L/capture" GSQL_GATE=/ timeout -k 5 60 ./gbasic "$LIFE" 2>&1)
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
if [ "$ms" -lt 2500 ] && printf '%s\n' "$out" | grep -q "starter exits"; then
    pass "lifetime: \$(...) returned in $ms ms, without waiting out the owner's 3 s linger"
else
    fail "lifetime: \$(...) took $ms ms: the owner holds the program's output open"
fi

GSQL_ROLE=refusals GSQL_DB="$L/refuse" timeout -k 5 60 ./gbasic "$LIFE" >"$L/refuse.out" 2>&1
if grep -q MISMATCH "$L/refuse.out" || [ "$(grep -c '^ok' "$L/refuse.out")" != 5 ]; then
    sed 's/^/       /' "$L/refuse.out" | head -8
    fail "lifetime: the linger option's refusals"
else
    pass "lifetime: linger out of range, of the wrong kind or misspelt is refused by name; 2 and 0.25 connect"
fi

printf 'TIER blocks: the `database` and `sql` blocks, every answer written by hand\n'
BLOCKS="$PWD/tests/gsql/blocks_test.bas"
mkdir -p "$work/blocks"
(cd "$work/blocks" && timeout -k 5 120 "$OLDPWD/gbasic" "$BLOCKS") >"$work/blocks.out" 2>"$work/blocks.err"
verdict blocks "$work/blocks.out" $? 60
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
sql overflowing on t
    select k + 9223372036854775807 as big from n order by k
end sql
function stopped()
    on error goto next
    on warning stop
    for each row in overflowing
        c = c + 1
    next
    if error then
        return error.code
    end if
    return nothing
end function
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
print to error "MARK stop"
s = stopped()
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
    rse=$(between raise stop)
    stp=$(between stop return)
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
    # a warning made an error by `on warning stop` on the first batch must not
    # leave the cursor it arrived with open in the owner
    if [ "$stp" = "QUERY CLOSE" ]; then
        pass "cursor: a first batch whose warning is an error closes its cursor: $stp"
    else
        fail "cursor: a first batch whose warning is an error sent \"$stp\", want \"QUERY CLOSE\""
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
# no `file:` is a database chosen at run time (gsql_block_connection_design.md),
# so creating and lingering are refused: they are the connection's
load_case role_create 2:1 'belongs to the connection that is made for it' 'load gsql
database db( create: true )'
load_case role_linger 2:1 '`linger:` belongs to the connection' 'load gsql
database db( linger: 0 )'
load_case twice 4:1 "declared twice" "$HEAD
database db( file: \"y.gsql\" )"
load_case in_function 5:5 "cannot be declared inside a function" "$HEAD
function f()
    sql q on db
        select 1
    end sql
    return nothing
end function"
# several statements are allowed (gsql_multi_statement_design.md); rows only
# from the last, and an empty statement is refused, each at its own place
load_case rows_not_last 5:5 "statement 1 returns rows, and only a block's last statement may" "$HEAD
sql q on db
    select 1; select 2
end sql"
load_case empty_statement 5:32 "an empty statement" "$HEAD
sql q on db
    create table a (x integer);;
    create table b (x integer)
end sql"
# a character SQL has not stops G-SQL's lexer partway through: the splitter
# once read a length it had never set and said "the statement is empty" at a
# `);` with nothing wrong with it (found from a hand-written program, a
# gBASIC {USD} modifier inside the SQL)
load_case lexing_failure 6:30 "unexpected character \`{\`" "$HEAD
sql q on db
    create table a (x integer, m money(USD));
    insert into a values (1, {USD}'1.00')
end sql"
load_case second_syntax 6:33 "sql 'q':" "$HEAD
sql q on db
    create table a (x integer);
    select id from a where id = = 1
end sql"
# captured first: the program is REFUSED, so gbasic exits 1, and under
# pipefail a `gbasic | grep -q` pipeline answers 1 even when grep matches
json=$(./gbasic --json-diagnostics "$work/load/syntax.bas" 2>&1 >/dev/null)
if printf '%s' "$json" | grep -q '"code":"GB_DIAG_GSQL_BLOCK".*"start":{"line":6,"column":26}'; then
    pass "load: the refusal reaches --json-diagnostics with its code and place"
else
    fail "load: --json-diagnostics did not carry GB_DIAG_GSQL_BLOCK at 6:26"
fi

# Warning 2121: a block nothing names never runs, and the program said nothing
# (found by Matthew trying blocks out). Asserted beside its control: the same
# program running its block is silent, and so is a table of the block's name
# inside the SQL, which the lexer keeps as one token.
printf 'TIER unused: a block that is declared and never used is said out loud, once, at its line\n'
cat > "$work/load/unused.bas" <<'GB'
load gsql
database db( file: "unused.gsql", create: true )

sql shares on db
    create table shares (id integer primary key)
end sql

sql used on db
    select 1 as one
end sql

print gsql.query(used)[0].one
GB
uout=$(cd "$work/load" && "$OLDPWD/gbasic" unused.bas 2>"$work/load/unused.err"); urc=$?
if [ "$urc" != 0 ] || [ "$uout" != 1 ]; then
    fail "unused: the program did not run as before the warning existed (exit $urc, out $uout)"
elif [ "$(grep -c '\[2121\]' "$work/load/unused.err")" != 1 ]; then
    sed 's/^/       /' "$work/load/unused.err"
    fail "unused: wanted exactly one warning 2121"
elif ! grep -q "sql 'shares' is declared and never used.*unused.bas:4:1" "$work/load/unused.err"; then
    sed 's/^/       /' "$work/load/unused.err"
    fail "unused: the warning does not name the block at its line"
else
    pass "unused: one warning 2121, naming 'shares' at 4:1; the block that is used, and the table of the same name, are silent"
fi
rm -rf "$work/load/unused.gsql"

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
    mkdir -p "$work/vglib"
    (cd "$work/vglib" && vg_run "$OLDPWD/gbasic" "$OLDPWD/tests/gsql/libblocks/libblocks_test.bas") >"$work/vglib.out" 2>"$work/vglib.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vglib.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: blocks inside libraries, clean over $(tail -1 "$work/vglib.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vglib.err"
        fail "valgrind objected to blocks inside libraries"
    else
        tail -3 "$work/vglib.out"
        fail "valgrind: the library blocks fixture did not pass under valgrind (exit $vgst)"
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

printf 'TIER leftovers: no owner outlives the suite\n'
# an owner stops by itself once it has had no connection for its linger
# (3 s by default), so this waits that long and more
left=""
for _ in $(seq 1 100); do
    left=$(pgrep -f -- "--gsql-owner (--linger=[0-9]+ )?$work/" | tr '\n' ' ')
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
