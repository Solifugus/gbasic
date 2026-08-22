#!/usr/bin/env bash
# PLAT-WEB-2 — the worker pool: a held listener, workers that inherit it over
# the LISTEN_FDS protocol, drain on SIGTERM (in-flight finishes, the worker
# exits itself), the application-level `server.draining` soft stop, and the
# rolling reload whose rule is "never retire a worker on faith".
#
# DETERMINISM. No fixture steps on a timer. In-flight requests are held by a
# gate FILE the driver creates; readiness is a marker the worker PRINTS;
# "the request is parked in the handler" is a line the worker writes to
# stderr, waited for, never assumed. Ports are all OS-assigned (port 0), so
# the goldens carry no numbers this machine chose.
#
# curl is the client throughout: an HTTP request from outside the interpreter
# is the point, and webclient would test our stack with our stack.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP tests/web_pool (curl not installed)\n'
    exit 0
fi

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
stdout_file="$(mktemp)"
trap 'rm -rf "$scratch" "$stdout_file"' EXIT

fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# --- golden cases -----------------------------------------------------------
for name in pool_basic pool_rolling pool_drain inherit_none; do
    rm -f "$scratch/gate.flag"
    : >"$stdout_file"
    if ! timeout 120 ./gbasic --line-buffered "tests/web_pool/$name.bas" "$scratch" \
            >"$stdout_file" 2>/dev/null; then
        cat "$stdout_file"; fail "$name (nonzero exit)"
    fi
    if diff -u "tests/web_pool/$name.out" "$stdout_file"; then
        printf 'PASS %s\n' "$name"
    else
        fail "$name (output diff)"
    fi
done

# --- drain_app: driven from OUTSIDE, because a single-process server only ---
# serves from the post-main event loop. The process exiting BY ITSELF after
# /last is the assertion that draining releases the loop.
: >"$stdout_file"
timeout 60 ./gbasic --line-buffered tests/web_pool/drain_app.bas >"$stdout_file" 2>/dev/null &
srv=$!
port=""
for _ in $(seq 1 100); do
    port="$(sed -n 's/^PORT //p' "$stdout_file" | head -1)"
    [ -n "$port" ] && break
    sleep 0.05
done
[ -n "$port" ] || { kill "$srv" 2>/dev/null; fail "drain_app (no port announced)"; }
first="$(curl -s -m 5 "http://127.0.0.1:$port/")"
second="$(curl -s -m 5 "http://127.0.0.1:$port/last")"
late="$(curl -s -m 1 "http://127.0.0.1:$port/late" || true)"
if wait "$srv"; then srv_exit=0; else srv_exit=$?; fi
[ "$first" = "answer 1" ]  || fail "drain_app (first answer was '$first')"
[ "$second" = "answer 2" ] || fail "drain_app (second answer was '$second')"
[ -z "$late" ]             || fail "drain_app (a request AFTER draining was answered: '$late')"
[ "$srv_exit" -eq 0 ]      || fail "drain_app (server did not exit cleanly: $srv_exit)"
printf 'PASS drain_app (served, drained on request, exited by itself)\n'

# --- negatives: byte-exact stderr, nonzero exit -----------------------------
for name in negative_listen_fds_type negative_hold_type; do
    : >"$stdout_file"
    if ./gbasic "tests/web_pool/$name.bas" >/dev/null 2>"$stdout_file"; then
        fail "$name (expected nonzero exit)"
    fi
    if diff -u "tests/web_pool/$name.err" "$stdout_file"; then
        printf 'PASS %s\n' "$name"
    else
        fail "$name (stderr diff)"
    fi
done

printf 'run_web_pool: all cases passed\n'
