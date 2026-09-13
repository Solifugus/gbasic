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
server_port_file="examples/gbasic_site/server_port.txt"
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
    rm -f "$port_file" "$server_stdout" "$server_stderr" "$client_stdout" "$client_stderr"
    rm -f "$server_port_backup"
}
trap cleanup EXIT

if [[ -e "$server_port_file" ]]; then
    cp "$server_port_file" "$server_port_backup"
    had_server_port=1
fi
rm -f "$port_file"
GBASIC_SITE_PORT=0 GBASIC_WEBSERVER_TIMEOUT=0.2 GBASIC_PATH=stdlib \
    ./gbasic --line-buffered examples/gbasic_site/site.bas \
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

# THE STOP IS A SIGNAL, NOT A ROUTE, and that is the half this tier gained when
# the site moved onto a `server` block. It used to be shut down by an
# unauthenticated GET /shutdown -- a remote kill switch, in the file people are
# most likely to copy. A block server's supported soft stop is SIGTERM: the
# `on drain` hook runs and the process exits ITSELF with code 0. All three are
# asserted, because a server killed by the trap would look identical to one
# that drained, and a hook that never ran would too.
kill -TERM "$server_pid"
server_status=0
if wait "$server_pid"; then
    server_status=0
else
    server_status=$?
fi
server_pid=""

if [[ "$server_status" != "0" ]]; then
    printf 'FAIL examples/gbasic_site/site.bas (SIGTERM gave exit %s, want 0)\n' "$server_status"
    cat "$server_stderr"
    exit 1
fi

if ! grep -q 'gbasic_site draining' "$server_stdout"; then
    printf 'FAIL examples/gbasic_site/site.bas (the on drain hook did not run)\n'
    cat "$server_stdout"
    exit 1
fi

if [[ -s "$server_stderr" ]]; then
    cat "$server_stderr"
    exit 1
fi

# A golden mismatch must FAIL. `if diff ...; then PASS; fi` prints the diff and
# then exits 0, because `set -e` does not fire on a command in an `if`
# condition -- so this suite reported OK to run_all.sh on a moved golden, which
# is a gate that cannot go red.
if ! diff -u tests/gbasic_site_client.out "$client_stdout"; then
    printf 'FAIL examples/gbasic_site/site.bas (output does not match the golden)\n'
    exit 1
fi
printf 'PASS examples/gbasic_site/site.bas\n'
