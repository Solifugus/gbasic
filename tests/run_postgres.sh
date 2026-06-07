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
