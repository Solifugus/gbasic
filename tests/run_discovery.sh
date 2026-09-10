#!/usr/bin/env bash
# `discovery` -- what a database ESTATE says about itself (stdlib/discovery.bas,
# docs/discovery_design.md). THIS INCREMENT READS DECLARED FACTS ONLY.
#
# WHY DECLARED-ONLY IS THE DESIGN AND NOT A STOPPING POINT. An inferred
# relationship is the result of a SEARCH, and a search over a 500-table estate
# is ~50 million candidate pairs -- at that width coincidences are a CERTAINTY,
# not a risk, since every `status` overlaps every other `status` and every
# surrogate key is 1..N. That is Recipe 1's finding one domain over, and
# inference arrives with a null model or not at all. So the sharpest assertion
# in the fixture is a COUNT: exactly one edge, because the database declared
# exactly one, and nothing was invented beyond it.
#
# SELF-CHECKING RATHER THAN GOLDEN, forced twice over. Every defect produces a
# PLAUSIBLE CATALOG -- an id missing its qualifier still looks like an id, a
# column list in the wrong order still looks like a column list. And THE
# EXPECTED VALUES DIFFER BY DATABASE (`sales.disc_orders` on SQLite,
# `sales.gbasic_test.public.disc_orders` on PostgreSQL), so a golden could only
# ever have pinned one of the four.
#
# THE LOAD-BEARING TIER IS IDENTITY, and it is asserted PORTABLY because the
# qualifier is not in the same place on any two databases: MariaDB puts the
# database in TABLE_CAT and leaves TABLE_SCHEM empty, PostgreSQL and SQL Server
# populate both, SQLite neither. An id built as `schema.table` yields
# `.disc_orders` on MariaDB -- silently, in a library whose whole job is
# identifying columns. So the fixture asserts what must hold everywhere: the id
# starts with the source, ends with the object, differs between two tables, and
# CONTAINS NO EMPTY SEGMENT (`sales..disc_orders` is exactly what a naive join
# produces where a qualifier is absent).
#
# Hermetic by default (SQLite over ODBC). GBASIC_ODBC_CONNECTION runs the SAME
# fixture against MariaDB, PostgreSQL or SQL Server, and it passed 25/25 on all
# four on 2026-09-08 -- which is the only reason the identity rules above are
# claims rather than guesses.
#
# KNOWN, AND NOT SUPPRESSED: pointed at MariaDB, the valgrind tier reddens on
# uninitialised reads inside libmaodbc.so's own SQLTables and SQLColumns. It is
# driver-internal (every frame is in the driver) and pre-dates any of this, but
# tests/odbc.supp covers only that driver's SQLPrepare path, and the policy in
# that file is that a suppression is isolated in plain C BEFORE it is written --
# otherwise it is indistinguishable from hiding our own defect. So this is
# recorded rather than silenced, and the default hermetic run is clean.
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# READING SQL NEEDS NO DATABASE, AND THIS TIER RUNS BEFORE THE DRIVER GATE.
# The parser used to be exercised only from inside the catalog fixture, so on
# a machine with no ODBC driver the whole of it -- statements, derivations,
# column lineage, every refusal -- was skipped along with the connection it
# did not need. A gate you can turn off by not installing something is a gate
# that shrinks.
printf 'TIER reading SQL (no database)\n'
if ! timeout -k 5 120 ./gbasic tests/discovery_sql_test.bas >"$work/sql.out" 2>"$work/sql.err"; then
    cat "$work/sql.err"; fail "the SQL fixture did not run"
elif grep -q MISMATCH "$work/sql.out"; then
    grep MISMATCH "$work/sql.out"; fail "the reader disagreed with what the SQL says"
elif ! grep -qx 'mismatches: 0' "$work/sql.out"; then
    fail "the SQL fixture did not finish"
elif [ -s "$work/sql.err" ]; then
    cat "$work/sql.err"; fail "the SQL fixture wrote to stderr"
else
    n=$(sed -n 's/^checks: //p' "$work/sql.out")
    if [ -z "$n" ] || [ "$n" -lt 60 ]; then
        fail "only ${n:-0} SQL checks ran, wanted at least 60"
    else
        pass "$n checks (references, projection, predicates, explain, statements, derivations)"
    fi
fi

printf 'TIER nothing is inferred\n'
# A TRIPWIRE, not a behavioural test. The library must not grow value-overlap
# or name-similarity matching without a null model arriving at the same time --
# and the fixtures cannot see that, because a tool that ALSO reports the real
# edge passes every check in them. Reading the source is the only thing that
# catches an inference sneaking in.
# COMMENT LINES ARE STRIPPED FIRST. The library's own prose explains at length
# why it does not infer, and a tripwire that fires on the explanation would be
# noise -- which is how a tripwire gets disabled and stops guarding anything.
hits=$(command sed "s/^[[:space:]]*'.*//" stdlib/discovery.bas \
       | command grep -nE '\b(overlap|similarity|jaccard|levenshtein|infer)' || true)
if [ -n "$hits" ]; then
    printf '%s\n' "$hits"
    fail "this increment declares itself declared-only; inference needs a null model first"
else
    pass "the library reads and does not guess"
fi

driver="${GBASIC_ODBC_DRIVER:-SQLite3}"
have_driver=1
if [[ -z "${GBASIC_ODBC_CONNECTION:-}" ]]; then
    if ! command -v odbcinst >/dev/null 2>&1; then
        have_driver=0
    elif ! odbcinst -q -d 2>/dev/null | command grep -qx "\[$driver\]"; then
        have_driver=0
    fi
fi

if [ "$have_driver" -eq 0 ]; then
    printf 'TIER declared facts\n'
    pass "SKIP (no $driver ODBC driver installed)"
    printf 'TIER valgrind\n'
    if vg_available; then
        if vg_run ./gbasic tests/discovery_sql_test.bas >/dev/null 2>"$work/vg.err"; then
            pass "no definite leak or invalid access"
        else
            cat "$work/vg.err"; fail "valgrind"
        fi
    else
        pass "SKIP (valgrind unavailable)"
    fi
    [ "$status" -ne 0 ] && exit 1
    printf 'PASS tests/run_discovery.sh\n'
    exit 0
fi

# The label must name what was ACTUALLY tested. With a connection string
# supplied the driver variable is unused, and printing it claimed a run
# against SQLite3 that never happened.
if [[ -n "${GBASIC_ODBC_CONNECTION:-}" ]]; then
    label="the supplied connection"
else
    label="$driver"
fi
export GBASIC_ODBC_CONNECTION="${GBASIC_ODBC_CONNECTION:-Driver=$driver;Database=$work/discovery.db}"

printf 'TIER declared facts, against %s\n' "$label"
if ! timeout -k 5 180 ./gbasic tests/discovery_test.bas >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the catalog disagreed with what was declared"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
elif [ -s "$work/err" ]; then
    cat "$work/err"; fail "the fixture wrote to stderr"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    # A coverage floor: a fixture that stops running its checks otherwise
    # passes by asserting nothing.
    if [ -z "$n" ] || [ "$n" -lt 25 ]; then
        fail "only ${n:-0} checks ran, wanted at least 25"
    else
        pass "$n checks (identity, ordinal order, declared keys, estate keying, traced chain)"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/discovery_sql_test.bas >/dev/null 2>"$work/vg1.err" \
       && vg_run ./gbasic tests/discovery_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_discovery.sh\n'
