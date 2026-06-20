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
port_file="examples/gbasic_site/tmp_port.txt"
server_port_file="examples/gbasic_site/server_port.txt"
admin_token_file="examples/gbasic_site/admin_token.txt"
csrf_token_file="examples/gbasic_site/csrf_token.txt"
server_port_backup="$(mktemp)"
had_server_port=0
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
    if [[ "$had_server_port" == "1" ]]; then
        cp "$server_port_backup" "$server_port_file"
    else
        rm -f "$server_port_file"
    fi
    rm -f "$setup_stdout" "$setup_stderr" "$check_stdout" "$check_stderr" \
        "$server_stdout" "$server_stderr" "$client_stdout" "$client_stderr" \
        "$port_file" "$admin_token_file" "$csrf_token_file"
    rm -f "$server_port_backup"
}
trap cleanup EXIT

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

if [[ -e "$server_port_file" ]]; then
    cp "$server_port_file" "$server_port_backup"
    had_server_port=1
fi
rm -f "$port_file"
printf '0\n' >"$server_port_file"
printf 'test-admin-token\n' >"$admin_token_file"
printf 'test-csrf-token\n' >"$csrf_token_file"
GBASIC_WEBSERVER_TIMEOUT=0.2 ./gbasic examples/gbasic_site/site_postgres.bas \
    >"$server_stdout" 2>"$server_stderr" &
server_pid=$!

for _ in {1..100}; do
    if [[ -s "$port_file" ]]; then
        break
    fi
    if ! kill -0 "$server_pid" 2>/dev/null; then
        printf 'FAIL examples/gbasic_site/site_postgres.bas (server exited before publishing its port)\n'
        cat "$server_stderr"
        exit 1
    fi
    sleep 0.05
done

if [[ ! -s "$port_file" ]]; then
    printf 'FAIL examples/gbasic_site/site_postgres.bas (server did not publish its port)\n'
    exit 1
fi

port="$(cat "$port_file")"
if ! python3 tests/gbasic_site_postgres_client.py "$port" >"$client_stdout" 2>"$client_stderr"; then
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
    printf 'FAIL examples/gbasic_site/site_postgres.bas (server did not shut down)\n'
    exit 1
fi

if [[ -s "$server_stderr" ]]; then
    cat "$server_stderr"
    exit 1
fi

if diff -u tests/gbasic_site_postgres_client.out "$client_stdout"; then
    printf 'PASS examples/gbasic_site/site_postgres.bas\n'
fi
