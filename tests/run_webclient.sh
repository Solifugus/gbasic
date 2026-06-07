#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_WEBCLIENT_TEST:-1}" != "1" ]]; then
    printf 'SKIP tests/webclient_integration.bas (GBASIC_WEBCLIENT_TEST is disabled)\n'
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP tests/webclient_integration.bas (python3 is unavailable)\n'
    exit 0
fi

make

ready_file="$(mktemp)"
server_error="$(mktemp)"
stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$ready_file" "$server_error" "$stdout_file" "$stderr_file"
}
trap cleanup EXIT

python3 tests/webclient_fixture.py --port 18765 >"$ready_file" 2>"$server_error" &
server_pid=$!

for _ in {1..50}; do
    if grep -q '^READY$' "$ready_file"; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'SKIP tests/webclient_integration.bas (loopback networking unavailable)\n'
        exit 0
    fi
    sleep 0.05
done

if ! grep -q '^READY$' "$ready_file"; then
    printf 'SKIP tests/webclient_integration.bas (fixture server did not start)\n'
    exit 0
fi

if ./gbasic tests/webclient_integration.bas >"$stdout_file" 2>"$stderr_file"; then
    if diff -u tests/webclient_integration.out "$stdout_file"; then
        printf 'PASS tests/webclient_integration.bas\n'
    else
        exit 1
    fi
else
    status=$?
    cat "$stderr_file"
    exit "$status"
fi
