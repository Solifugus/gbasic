#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_SITE_POSTGRES_TEST:-0}" != "1" ]]; then
    printf 'SKIP examples/gbasic_site Postgres checks (set GBASIC_SITE_POSTGRES_TEST=1 and standard PG* connection variables)\n'
    exit 0
fi

if [[ -z "${PGDATABASE:-}" || -z "${PGUSER:-}" ]]; then
    printf 'SKIP examples/gbasic_site Postgres checks (set PGDATABASE and PGUSER)\n'
    exit 0
fi

make

setup_stdout="$(mktemp)"
setup_stderr="$(mktemp)"
check_stdout="$(mktemp)"
check_stderr="$(mktemp)"
trap 'rm -f "$setup_stdout" "$setup_stderr" "$check_stdout" "$check_stderr"' EXIT

if ! ./gbasic examples/gbasic_site/setup.bas >"$setup_stdout" 2>"$setup_stderr"; then
    cat "$setup_stderr"
    exit 1
fi

if [[ "$(cat "$setup_stdout")" != "gbasic_site database ready" ]]; then
    printf 'FAIL examples/gbasic_site/setup.bas\n'
    printf 'unexpected setup output:\n'
    cat "$setup_stdout"
    exit 1
fi

if ./gbasic tests/gbasic_site_postgres_check.bas >"$check_stdout" 2>"$check_stderr"; then
    if diff -u tests/gbasic_site_postgres_check.out "$check_stdout"; then
        printf 'PASS examples/gbasic_site Postgres checks\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$check_stderr"
    exit "$status"
fi
