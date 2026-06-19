#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ "${GBASIC_SITE_TEST:-1}" != "1" ]]; then
    printf 'SKIP examples/gbasic_site/site.bas (GBASIC_SITE_TEST is disabled)\n'
    exit 0
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP examples/gbasic_site/site.bas (python3 is unavailable)\n'
    exit 0
fi

make

port_file="examples/gbasic_site/tmp_port.txt"
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
GBASIC_WEBSERVER_TIMEOUT=0.2 ./gbasic examples/gbasic_site/site.bas \
    >"$server_stdout" 2>"$server_stderr" &
server_pid=$!

for _ in {1..100}; do
    if [[ -s "$port_file" ]]; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'FAIL examples/gbasic_site/site.bas (server exited before publishing its port)\n'
        cat "$server_stderr"
        exit 1
    fi
    sleep 0.05
done

if [[ ! -s "$port_file" ]]; then
    printf 'FAIL examples/gbasic_site/site.bas (server did not publish its port)\n'
    exit 1
fi

port="$(cat "$port_file")"
if ! python3 tests/gbasic_site_client.py "$port" >"$client_stdout" 2>"$client_stderr"; then
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
    printf 'FAIL examples/gbasic_site/site.bas (server did not shut down)\n'
    exit 1
fi

if [[ -s "$server_stderr" ]]; then
    cat "$server_stderr"
    exit 1
fi

if diff -u tests/gbasic_site_client.out "$client_stdout"; then
    printf 'PASS examples/gbasic_site/site.bas\n'
fi
