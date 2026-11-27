#!/bin/bash
# VECTORS IN G-SQL (docs/gsql_vector_design.md), increment 2 of the
# AI-engineering work: a VECTOR(N) column, pgvector's three distances (<->,
# <=>, <#>) and the functions naming them, and exact search by distance, the
# filter and the ranking in one statement.
#
# TIERS:
#   semantics   tests/gsql_vector/sql.bas: every answer worked by hand (vectors
#               chosen so each distance is exact), each refusal beside an
#               accepted neighbour; precedence; CAST and text; gsql.columns and
#               gsql.expect; a dump and restore to the same serialize bytes
#   oracle      tests/gsql_vector/oracle.py makes a table and 24 queries per
#               seed (3, 17, 64 and 1536 dimensions, one a seed, copies for ties, every spelling,
#               a filter or none) and judges G-SQL's answers EXACTLY from its
#               own arithmetic, with controls that the sum order and the
#               metric could each have been caught wrong
#   parse       the three operators lex as one token each, and a form that
#               parsed before (`a<-1`, `a <= b`) still parses as it did
#   cost        an exact search through SQL within 15x of nearest() over the
#               same vectors in memory, in one process (measured ~6x), and the
#               two agree on the answer
#   valgrind    the semantics fixture, no invalid access, no definite leak
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/premise.sh"   # premise_time_claims
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
    printf 'run_gsql_vector: FAILED (./gbasic did not build)\n'
    exit 1
fi
printf 'load gsql\nprint "loaded"\n' > "$work/probe.bas"
if ! probe=$(./gbasic "$work/probe.bas" 2>&1) || [ "$probe" != loaded ]; then
    printf 'SKIP run_gsql_vector (this build has no G-SQL: %s -- vectors in G-SQL were NOT measured)\n' "$probe"
    exit 0
fi
export GBASIC_PATH="$PWD/stdlib"
root="$PWD"

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

printf 'TIER semantics: every answer worked by hand, each refusal beside its neighbour\n'
mkdir -p "$work/sem"
(cd "$work/sem" && timeout -k 5 120 "$root/gbasic" "$root/tests/gsql_vector/sql.bas") \
    >"$work/sem.out" 2>"$work/sem.err"
verdict semantics "$work/sem.out" $? 85
if [ -s "$work/sem.err" ]; then
    sed 's/^/       /' "$work/sem.err" | head -5
    fail "semantics: the fixture wrote to stderr"
fi

printf 'TIER oracle: search by distance judged exactly by Python, never from what gBASIC printed\n'
if command -v python3 >/dev/null 2>&1; then
    for seed in 1 2 3 4; do
        mkdir -p "$work/or$seed"
        python3 tests/gsql_vector/oracle.py gen "$seed" "$work/or$seed/cases"
        (cd "$work/or$seed" && timeout -k 5 300 "$root/gbasic" "$root/tests/gsql_vector/oracle.bas" cases) \
            >"$work/or$seed/answers" 2>"$work/or$seed/err"
        rc=$?
        if [ "$rc" != 0 ] || [ -s "$work/or$seed/err" ]; then
            head -5 "$work/or$seed/err"
            fail "oracle seed $seed: the fixture exited $rc"
            continue
        fi
        if out=$(python3 tests/gsql_vector/oracle.py check "$work/or$seed/cases" "$work/or$seed/answers"); then
            pass "oracle seed $seed ($(head -1 "$work/or$seed/cases")): $(printf '%s\n' "$out" | tail -1)"
        else
            printf '%s\n' "$out" | head -12
            fail "oracle seed $seed"
        fi
    done
else
    printf '  SKIP oracle: no python3 -- search was NOT judged against an outside oracle\n'
fi

printf 'TIER parse: <->, <=> and <#> are one token each, and what parsed before parses as it did\n'
mkdir -p "$work/parse"
cat > "$work/parse/p.bas" <<'EOF'
load gsql
db = gsql.connect("p.gsql", { create: true })
db.exec("create table t (id integer primary key, n number, e vector)")
db.exec("insert into t values (1, -3, '[1, 0]'), (2, 4, '[0, 1]')")
print db.query("select id from t where n<-1")
print db.query("select id from t where n <= 4 and n>-4 order by id")
print db.query("select id from t where n< -1")
print db.query("select e<->'[1,0]' as a, e<=>'[1,0]' as b, e<#>'[1,0]' as c from t order by id")
print db.query("select (e <-> '[0,0]') - 1 as v from t where id = 1")
EOF
(cd "$work/parse" && timeout -k 5 60 "$root/gbasic" p.bas) >"$work/parse.out" 2>&1
cat > "$work/parse.want" <<'EOF'
[{"id":1}]
[{"id":1},{"id":2}]
[{"id":1}]
[{"a":0,"b":0,"c":-1},{"a":1.4142135623730951,"b":1,"c":-0}]
[{"v":0}]
EOF
if diff "$work/parse.want" "$work/parse.out" >"$work/parse.diff"; then
    pass "parse: n<-1 is n < -1, <= and > as before; the three distances without spaces"
else
    sed 's/^/       /' "$work/parse.diff" | head -12
    fail "parse"
fi

printf 'TIER cost: an exact search through SQL within 15x of nearest() in memory\n'
if premise_time_claims; then
    mkdir -p "$work/cost"
    cat > "$work/cost/c.bas" <<'EOF'
load gsql
width = 1536
bases = []
for b = 0 to 99
    xs = []
    for i = 0 to width - 1
        append(xs, random() - 0.5)
    next
    append(bases, vector(xs))
next
vs = []
rows = []
for i = 0 to 4999
    v = bases[mod(i, 100)] + bases[mod(i * 7 + 3, 100)] * (mod(i, 977) / 977)
    append(vs, v)
    append(rows, { id: i, e: v })
next
db = gsql.connect("c.gsql", { create: true })
db.exec("create table doc (id integer primary key, e vector(1536))")
gsql.insert(db, "doc", rows)
q = bases[17] * 0.3 + bases[40]
best_sql = 1e9
best_mem = 1e9
same = 0
for k = 1 to 3
    t0 = monotonic()
    got = db.query("select id, e <=> ? as d from doc order by e <=> ? limit 10", [q, q])
    t1 = monotonic()
    hits = nearest(q, vs, 10, "cosine")
    t2 = monotonic()
    if t1 - t0 < best_sql then
        best_sql = t1 - t0
    end if
    if t2 - t1 < best_mem then
        best_mem = t2 - t1
    end if
    same = 0
    for i = 0 to 9
        if got[i].id = hits[i].index and got[i].d = hits[i].distance then
            same = same + 1
        end if
    next
next
print string(best_sql / best_mem) + " " + string(same)
EOF
    if out=$(cd "$work/cost" && timeout -k 5 300 "$root/gbasic" c.bas 2>&1); then
        ratio=${out%% *}
        agree=${out##* }
        if [ "$agree" != 10 ]; then
            fail "cost: SQL and nearest() agree on $agree of 10"
        elif awk -v r="$ratio" 'BEGIN { exit !(r <= 15) }'; then
            pass "cost: SQL exact top 10 of 5000x1536 is ${ratio}x nearest() in memory (bound 15x), the same 10"
        else
            fail "cost: SQL exact search is ${ratio}x nearest() in memory, past the bound of 15x"
        fi
    else
        printf '%s\n' "$out" | head -5
        fail "cost: the fixture failed"
    fi
else
    printf '  SKIP cost: GATE_TIME_SCALE says this machine is slower, and the tier asserts a speed\n'
fi

printf 'TIER valgrind: the semantics fixture, no invalid access, no definite leak\n'
if vg_available; then
    mkdir -p "$work/vg"
    (cd "$work/vg" && vg_run "$root/gbasic" "$root/tests/gsql_vector/sql.bas") >"$work/vg.out" 2>"$work/vg.err"
    vgst=$?
    if [ "$vgst" = 0 ] && tail -1 "$work/vg.out" | grep -q ' 0 mismatches$'; then
        pass "valgrind: clean over $(tail -1 "$work/vg.out" | sed 's/^ok //')"
    elif [ "$vgst" = "$VG_EXIT" ]; then
        grep -m8 'Invalid\|definitely lost\|uninitialised' "$work/vg.err"
        fail "valgrind objected"
    else
        tail -3 "$work/vg.out"
        fail "valgrind: the fixture did not pass under valgrind (exit $vgst)"
    fi
else
    printf '  SKIP valgrind: not installed\n'
fi

if [ "$status" = 0 ]; then
    printf 'PASS tests/run_gsql_vector.sh\n'
else
    printf 'FAIL tests/run_gsql_vector.sh\n'
fi
exit "$status"
