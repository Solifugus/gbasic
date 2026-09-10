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
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

driver="${GBASIC_ODBC_DRIVER:-SQLite3}"
if [[ -z "${GBASIC_ODBC_CONNECTION:-}" ]]; then
    if ! command -v odbcinst >/dev/null 2>&1; then
        printf 'SKIP tests/run_discovery.sh (odbcinst not available to find a driver)\n'
        exit 0
    fi
    if ! odbcinst -q -d 2>/dev/null | command grep -qx "\[$driver\]"; then
        printf 'SKIP tests/run_discovery.sh (no %s ODBC driver installed)\n' "$driver"
        exit 0
    fi
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
export GBASIC_ODBC_CONNECTION="${GBASIC_ODBC_CONNECTION:-Driver=$driver;Database=$work/discovery.db}"

status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER declared facts, against %s\n' "$driver"
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
    if [ -z "$n" ] || [ "$n" -lt 45 ]; then
        fail "only ${n:-0} checks ran, wanted at least 45"
    else
        pass "$n checks (identity, ordinal order, declared keys, estate keying, SQL references, traced chain)"
    fi
fi

printf 'TIER nothing is inferred\n'
# A TRIPWIRE, not a behavioural test. The library must not grow value-overlap
# or name-similarity matching without a null model arriving at the same time --
# and the fixture above cannot see that, because a tool that ALSO reports the
# real edge passes every check in it. Reading the source is the only thing that
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

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/discovery_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_discovery.sh\n'
