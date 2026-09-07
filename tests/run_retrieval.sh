#!/usr/bin/env bash
# `retrieval` -- permission-filtered nearest-neighbour search over pgvector.
# Step 7 of docs/gbasic_ai_reference_and_primitives.md (§1.7).
#
# THE WHOLE POINT IS THAT THE PERMISSION FILTER AND THE RANKING ARE ONE QUERY.
# The obvious implementation ranks first and drops what the caller may not see,
# and it fails in a way that looks like an ordinary empty result: a user with
# narrow permissions asks a question, the globally nearest chunks all belong to
# someone else, every one is dropped, and they are told nothing matched.
# Nothing errors, and their own best matches were never considered.
#
# Tiers:
#   OFFLINE  always runs, so this suite asserts something even where PostgreSQL
#            is absent -- an entirely skippable suite is one that can go quiet.
#            What it can establish is STRUCTURAL (the predicate precedes the
#            ordering, the acl is indexed), which is weak alone and is why the
#            live tier proves the same thing behaviourally.
#   LIVE     the difference, against a real database: a corpus where the three
#            nearest chunks are ones the asking user MAY NOT SEE, and they still
#            get their own top-2. Its control is a user who MAY see them getting
#            those instead -- without it, "the narrow user got two rows" is
#            equally satisfied by an ACL that does nothing.
#   HASH     re-storing unchanged chunks writes nothing, which is what makes an
#            indexer resumable.
#   VALGRIND on the offline fixture, which needs no database.
#
# Live tiers need PostgreSQL with pgvector: set GBASIC_POSTGRES_TEST=1 and
# PGDATABASE, the same switch tests/run_postgres.sh uses.
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

run_fixture() { # file min label
    local f="$1" min="$2" label="$3"
    if timeout -k 5 180 ./gbasic "$f" >"$work/out" 2>"$work/err"; then
        local checks
        checks="$(sed -n 's/^checks: //p' "$work/out")"
        if ! grep -q '^mismatches: 0$' "$work/out"; then
            grep '^MISMATCH' "$work/out" || true
            fail "$label: the fixture disagreed with itself"
        elif [ -z "$checks" ] || [ "$checks" -lt "$min" ]; then
            fail "$label: only ${checks:-0} checks ran, wanted at least $min"
        else
            pass "$label ($checks checks)"
        fi
    else
        # A fixture that mismatched AND THEN died has already said what is
        # wrong; showing only "did not run" throws that away and points at the
        # crash instead of the cause.
        grep '^MISMATCH' "$work/out" 2>/dev/null || true
        cat "$work/err"
        fail "$label: did not run to completion"
    fi
}

printf 'TIER the query shape, with no database\n'
run_fixture tests/retrieval_offline_test.bas 15 "offline"

printf 'TIER filter-then-rank, against a real pgvector\n'
if [ "${GBASIC_POSTGRES_TEST:-0}" != "1" ] || [ -z "${PGDATABASE:-}" ]; then
    printf '  SKIP live tiers (set GBASIC_POSTGRES_TEST=1 and PGDATABASE)\n'
elif ! command -v psql >/dev/null 2>&1; then
    printf '  SKIP live tiers (psql is unavailable)\n'
elif ! psql -d "$PGDATABASE" -tAc "select 1 from pg_available_extensions where name='vector'" 2>/dev/null | grep -q 1; then
    printf '  SKIP live tiers (pgvector is not installed on this server)\n'
else
    run_fixture tests/retrieval_test.bas 23 "live"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/retrieval_offline_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_retrieval.sh\n'
