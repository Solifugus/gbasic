#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if ./gbasic examples/error_fatal_test.gb >/dev/null 2>&1; then
    printf 'FAIL examples/error_fatal_test.gb\n'
    exit 1
fi

printf 'PASS examples/error_fatal_test.gb\n'
