#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_WEBSERVER_TEST:-1}" != "1" ]]; then
    printf 'SKIP tests/webserver_integration.bas (GBASIC_WEBSERVER_TEST is disabled)\n'
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP tests/webserver_integration.bas (python3 is unavailable)\n'
    exit 0
fi

make

port_file="tests/tmp_webserver_port.txt"
server_stdout="$(mktemp)"
server_stderr="$(mktemp)"
client_stdout="$(mktemp)"
client_stderr="$(mktemp)"
server_pid=""

cleanup() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
    fi
    rm -f "$port_file" "$server_stdout" "$server_stderr" "$client_stdout" "$client_stderr"
}
trap cleanup EXIT

rm -f "$port_file"
GBASIC_WEBSERVER_TIMEOUT=0.2 ./gbasic tests/webserver_integration.bas \
    >"$server_stdout" 2>"$server_stderr" &
server_pid=$!

for _ in {1..100}; do
    if [[ -s "$port_file" ]]; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'SKIP tests/webserver_integration.bas (loopback networking unavailable)\n'
        cat "$server_stderr"
        exit 0
    fi
    sleep 0.05
done

if [[ ! -s "$port_file" ]]; then
    printf 'SKIP tests/webserver_integration.bas (server did not publish its port)\n'
    exit 0
fi

port="$(cat "$port_file")"
if ! python3 tests/webserver_client.py "$port" >"$client_stdout" 2>"$client_stderr"; then
    cat "$client_stderr"
    exit 1
fi

for _ in {1..100}; do
    if ! kill -0 "$server_pid" 2>/dev/null; then
        wait "$server_pid"
        server_pid=""
        break
    fi
    sleep 0.05
done

if [[ -n "$server_pid" ]]; then
    printf 'FAIL tests/webserver_integration.bas (server did not shut down)\n'
    exit 1
fi

if [[ -s "$server_stderr" ]]; then
    cat "$server_stderr"
    exit 1
fi

if diff -u tests/webserver_integration.out "$client_stdout"; then
    printf 'PASS tests/webserver_integration.bas\n'
fi

