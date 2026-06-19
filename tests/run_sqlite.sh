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
