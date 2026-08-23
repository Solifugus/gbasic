#!/usr/bin/env bash
# PLAT-WEB-5 — the declarative `server` block, last and only because 0-4
# proved the shape: the grammar (zero new reserved words), the §8 load-time
# checks in --json-diagnostics, the inert hoisted declaration, serve() over
# the WEB-1..4 runtime (host dispatch, static fallback, streams, the drain
# hook, the workers pool via process.self), and the §9 no-socket
# introspection (web.routes / web.dispatch on the declaration, the outline).
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch" tests/web_server_block/.certs' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

wait_port() { # logfile name
    local port="" i
    for i in $(seq 1 200); do
        port="$(sed -n 's/^PORT //p' "$1" | head -1)"
        [ -n "$port" ] && { printf '%s' "$port"; return 0; }
        sleep 0.05
    done
    fail "$2 (no port announced)"
}

# --- offline: routes as data, dispatch with no socket, the outline ---------
./gbasic tests/web_server_block/block_offline.bas 2>/dev/null >"$scratch/offline.got" \
    || fail "block_offline (exited nonzero)"
diff -u tests/web_server_block/block_offline.out "$scratch/offline.got" \
    || fail "block_offline (output diverged)"
printf 'PASS block_offline (routes as data, socketless dispatch, outline)\n'

./gbasic tests/web_server_block/block_hoist.bas 2>/dev/null >"$scratch/hoist.got" \
    || fail "block_hoist (exited nonzero)"
diff -u tests/web_server_block/block_hoist.out "$scratch/hoist.got" \
    || fail "block_hoist (output diverged)"
printf 'PASS block_hoist (a block below the program binds before it runs)\n'

# --- the §8 load-time refusals, every one located and named ----------------
neg() { # file expected...
    local file="$1"; shift
    if ./gbasic "tests/web_server_block/$file" >/dev/null 2>"$scratch/err"; then
        fail "$file (expected nonzero exit)"
    fi
    local want
    for want in "$@"; do
        grep -qF "$want" "$scratch/err" \
            || fail "$file (missing: $want; got: $(cat "$scratch/err"))"
    done
}

neg neg_routes.bas \
    "unknown verb 'fetch'" \
    "repeats the capture name 'id'" \
    "'get /a/{id}' and 'get /a/{name}' can never be told apart" \
    "duplicate route get \"/b\"" \
    "must start with /" \
    "a greedy capture may only be the LAST segment" \
    "duplicate route stream \"/b\""
printf 'PASS neg_routes (verbs, patterns, duplicates, ambiguity, stream-as-GET)\n'

neg neg_structure.bas \
    "root declared twice" \
    "unknown directive 'banana'" \
    "unknown hook 'on reload'" \
    "trust_proxy belongs to the server, not to a site" \
    "repeats host \"h.example\"" \
    "web 'one' declared twice" \
    "web 'three' needs a host"
printf 'PASS neg_structure (directives, hooks, hosts, site names)\n'

neg neg_head.bas \
    "option 'port' must be a number literal" \
    "unknown option 'magic'" \
    "server 'h' declared twice" \
    "server 'c' is closed by 'end lopsided'" \
    "unknown declarative block 'observer'"
printf 'PASS neg_head (literal options, duplicate names, closers, block words)\n'

neg neg_nested.bas "a server block cannot be declared inside a function"
printf 'PASS neg_nested\n'

./gbasic --json-diagnostics tests/web_server_block/neg_routes.bas 2>"$scratch/json" || true
grep -q '"code":"GB_DIAG_SERVER_BLOCK"' "$scratch/json" \
    || fail "json_diagnostics (no GB_DIAG_SERVER_BLOCK in: $(head -1 "$scratch/json"))"
printf 'PASS json_diagnostics (load-time checks reach --json-diagnostics)\n'

if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP tests/web_server_block live tiers (curl not installed)\n'
    printf 'run_web_server_block: offline cases passed\n'
    exit 0
fi

# --- the minimal example, served -------------------------------------------
log="$scratch/min.log"
timeout 60 ./gbasic --line-buffered tests/web_server_block/block_minimal.bas >"$log" 2>/dev/null &
srv=$!
port="$(wait_port "$log" block_minimal)"
[ "$(curl -s -m 5 "http://127.0.0.1:$port/")" = "hello from the block" ] \
    || fail "block_minimal (root route)"
[ "$(curl -s -m 5 "http://127.0.0.1:$port/products/42")" = "product 42" ] \
    || fail "block_minimal (capture)"
[ "$(curl -s -m 5 -d milk "http://127.0.0.1:$port/cart")" = "added milk" ] \
    || fail "block_minimal (POST body)"
[ "$(curl -s -m 5 "http://127.0.0.1:$port/page.txt")" = "a plain page" ] \
    || fail "block_minimal (static fallback)"
code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/nope")"
[ "$code" = "404" ] || fail "block_minimal (missing was $code)"
code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' -X POST "http://127.0.0.1:$port/")"
[ "$code" = "405" ] || fail "block_minimal (wrong verb was $code)"
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true
printf 'PASS block_minimal (routes, capture, body, static fallback, 404, 405)\n'

# --- host dispatch, with the serve handle bound only in a dead frame -------
log="$scratch/sites.log"
timeout 60 ./gbasic --line-buffered tests/web_server_block/block_sites.bas >"$log" 2>/dev/null &
srv=$!
port="$(wait_port "$log" block_sites)"
[ "$(curl -s -m 5 "http://127.0.0.1:$port/")" = "default site" ] \
    || fail "block_sites (default)"
[ "$(curl -s -m 5 -H 'Host: store.example' "http://127.0.0.1:$port/")" = "store site" ] \
    || fail "block_sites (store)"
[ "$(curl -s -m 5 -H 'Host: api.example' "http://127.0.0.1:$port/")" = "api site" ] \
    || fail "block_sites (api)"
code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/only-store")"
[ "$code" = "404" ] || fail "block_sites (site route leaked to default: $code)"
printf 'PASS block_sites (host dispatch; the server record is bound nowhere)\n'
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- streams: in-handler loop, park and poke, disconnect detection ---------
log="$scratch/stream.log"
timeout 90 ./gbasic --line-buffered tests/web_server_block/block_stream.bas >"$log" 2>/dev/null &
srv=$!
port="$(wait_port "$log" block_stream)"
body="$(curl -sN -m 10 "http://127.0.0.1:$port/tick")"
expected="$(printf 'data: tick 0\n\ndata: tick 1\n\ndata: tick 2\n')"
[ "$body" = "$expected" ] || fail "block_stream (tick got '$body')"
curl -sN -m 60 "http://127.0.0.1:$port/events" >"$scratch/events.out" &
listener=$!
poked=""
for _ in $(seq 1 100); do
    poked="$(curl -s -m 5 -X POST "http://127.0.0.1:$port/poke")"
    [ "$poked" = "poked 1" ] && break; sleep 0.05
done
[ "$poked" = "poked 1" ] || fail "block_stream (poke said '$poked')"
for _ in $(seq 1 100); do
    grep -q "data: hello" "$scratch/events.out" && break; sleep 0.05
done
grep -q "event: poke" "$scratch/events.out" || fail "block_stream (no named event at the client)"
kill "$listener" 2>/dev/null || true
wait "$listener" 2>/dev/null || true
poked=""
for _ in $(seq 1 100); do
    poked="$(curl -s -m 5 -X POST "http://127.0.0.1:$port/poke")"
    [ "$poked" = "poked 0" ] && break; sleep 0.05
done
[ "$poked" = "poked 0" ] || fail "block_stream (disconnect not noticed: '$poked')"
printf 'PASS block_stream (in-handler loop, park/poke via a global record, reap)\n'
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- drain: the hook runs, the process exits itself ------------------------
timeout 90 ./gbasic tests/web_server_block/drain_driver.bas >"$scratch/drain.got" 2>/dev/null \
    || fail "block_drain (driver exited nonzero: $(cat "$scratch/drain.got"))"
diff -u tests/web_server_block/drain_driver.out "$scratch/drain.got" \
    || fail "block_drain (output diverged)"
printf 'PASS block_drain (polite TERM: on drain hook, self-exit 0)\n'

# --- workers: 2 -- the supervisor self-spawns over the pool ----------------
timeout 120 ./gbasic tests/web_server_block/pool_driver.bas >"$scratch/pool.got" 2>/dev/null \
    || fail "block_pool (driver exited nonzero: $(cat "$scratch/pool.got"))"
diff -u tests/web_server_block/pool_driver.out "$scratch/pool.got" \
    || fail "block_pool (output diverged)"
printf 'PASS block_pool (workers: 2 via process.self, drained on TERM)\n'

# --- TLS from the head -----------------------------------------------------
if command -v openssl >/dev/null 2>&1; then
    mkdir -p tests/web_server_block/.certs
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout tests/web_server_block/.certs/main.key \
        -out tests/web_server_block/.certs/main.crt \
        -days 2 -subj "/CN=localhost" 2>/dev/null
    log="$scratch/tls.log"
    timeout 60 ./gbasic --line-buffered tests/web_server_block/block_tls.bas >"$log" 2>"$scratch/tls.err" &
    srv=$!
    if grep -q "not available in this build" "$scratch/tls.err" 2>/dev/null; then
        printf 'SKIP block_tls (gbasic built without libssl)\n'
    else
        port="$(wait_port "$log" block_tls)"
        body="$(curl -sk -m 5 "https://127.0.0.1:$port/")"
        [ "$body" = "secure https" ] || fail "block_tls (got '$body')"
        printf 'PASS block_tls (cert on the head becomes the default pair)\n'
    fi
    kill "$srv" 2>/dev/null || true
    wait "$srv" 2>/dev/null || true
else
    printf 'SKIP block_tls (openssl not installed)\n'
fi

printf 'run_web_server_block: all cases passed\n'
