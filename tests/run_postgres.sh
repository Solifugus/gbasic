#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_POSTGRES_TEST:-0}" != "1" ]]; then
    printf 'SKIP tests/postgres_integration.bas (set GBASIC_POSTGRES_TEST=1 and standard PG* connection variables)\n'
    exit 0
fi

make

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

if ./gbasic tests/postgres_integration.bas >"$stdout_file" 2>"$stderr_file"; then
    if diff -u tests/postgres_integration.out "$stdout_file"; then
        printf 'PASS tests/postgres_integration.bas\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$stderr_file"
    exit "$status"
fi

# SET-VALUED COLUMNS, held to what docs/reference.md says about them. Native
# Postgres arrays are unsupported both ways (the two "raises" lines are a
# NEGATIVE CONTROL that goes red when that changes -- update the reference,
# not the test), and a jsonb column plus pgvector's own type carry the same
# data on the module as it is. Self-checking rather than golden, because the
# defect class here is an ordinary-looking wrong row set. The pgvector tier
# skips itself when the extension is absent from $PGDATABASE.
#
# Found by checking the AI reference proposal against the tree: the first
# reading concluded native arrays blocked the design, and RUNNING the
# workaround reversed that. This file is what kept the reversal honest.
if ./gbasic tests/postgres_arrays.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q '^mismatches: 0$' "$stdout_file" \
       && [ "$(sed -n 's/^checks: //p' "$stdout_file")" -ge 10 ]; then
        printf 'PASS tests/postgres_arrays.bas (%s checks%s)\n' \
            "$(sed -n 's/^checks: //p' "$stdout_file")" \
            "$(grep -q '^SKIP pgvector' "$stdout_file" && printf ', pgvector tiers skipped')"
    else
        printf 'FAIL tests/postgres_arrays.bas\n'
        grep -E '^MISMATCH|^checks|^mismatches' "$stdout_file"
        exit 1
    fi
else
    status=$?
    printf 'FAIL tests/postgres_arrays.bas (exit %s)\n' "$status"
    cat "$stderr_file"
    exit "$status"
fi
