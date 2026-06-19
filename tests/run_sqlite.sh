#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists sqlite3; then
    printf 'SKIP tests/sqlite_integration.bas (sqlite3 development files not available)\n'
    exit 0
fi

make

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

if ./gbasic tests/sqlite_integration.bas >"$stdout_file" 2>"$stderr_file"; then
    if diff -u tests/sqlite_integration.out "$stdout_file"; then
        printf 'PASS tests/sqlite_integration.bas\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$stderr_file"
    exit "$status"
fi

negative_cases=(
    negative_sqlite_connect_type
    negative_sqlite_close_type
    negative_sqlite_query_arity
    negative_sqlite_exec_arity
    negative_sqlite_query_connection
    negative_sqlite_params_type
    negative_sqlite_param_count
    negative_sqlite_exec_rows
    negative_sqlite_blob_result
)

for name in "${negative_cases[@]}"; do
    source="tests/$name.bas"
    expected="tests/$name.err"
    : >"$stdout_file"
    : >"$stderr_file"

    if ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        printf 'FAIL %s\n' "$source"
        printf 'expected nonzero exit\n'
        exit 1
    fi

    if diff -u "$expected" "$stderr_file"; then
        printf 'PASS %s\n' "$source"
    else
        exit 1
    fi

    if [[ -s "$stdout_file" ]]; then
        printf 'FAIL %s\n' "$source"
        printf 'expected empty stdout\n'
        cat "$stdout_file"
        exit 1
    fi
done
