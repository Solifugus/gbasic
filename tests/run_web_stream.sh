#!/usr/bin/env bash
# PLAT-WEB-4 Gap E — streaming responses: an SSE handler that emits from
# inside its own handler, a parked stream poked from another request, the
# timeout exemption (a stream is not a slow response), file responses that
# stream bytes instead of slurping them into a gBASIC string, a drain that
# ENDS a parked stream, and the negative that keeps stream and body apart.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP tests/web_stream (curl not installed)\n'
    exit 0
fi

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
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

# --- the SSE server: in-handler emit, broadcast, timeout exemption ---------
log="$scratch/sse.log"
timeout 120 ./gbasic --line-buffered tests/web_stream/sse_server.bas >"$log" 2>/dev/null &
srv=$!
port="$(wait_port "$log" sse_tick)"

body="$(curl -sN -m 10 "http://127.0.0.1:$port/tick")"
expected="$(printf 'data: tick 0\n\ndata: tick 1\n\ndata: tick 2\n')"
[ "$body" = "$expected" ] || fail "sse_tick (got '$body')"
for _ in $(seq 1 100); do
    grep -q "tick done emits=3 finish=true" "$log" && break; sleep 0.05
done
grep -q "tick done emits=3 finish=true" "$log" \
    || fail "sse_tick (server said: $(grep 'tick done' "$log" || echo nothing))"
printf 'PASS sse_tick (three in-handler emits arrive, finish reports true)\n'

# park a stream, then poke it from a second connection
curl -sN -m 60 "http://127.0.0.1:$port/events" >"$scratch/events.out" &
listener=$!
poked=""
for _ in $(seq 1 100); do
    poked="$(curl -s -m 5 "http://127.0.0.1:$port/poke")"
    [ "$poked" = "poked 1" ] && break; sleep 0.05
done
[ "$poked" = "poked 1" ] || fail "sse_broadcast (poke said '$poked')"
for _ in $(seq 1 100); do
    grep -q "data: hello" "$scratch/events.out" && break; sleep 0.05
done
grep -q "event: poke" "$scratch/events.out" || fail "sse_broadcast (no event name reached the client)"
grep -q "data: hello" "$scratch/events.out" || fail "sse_broadcast (no event data reached the client)"
printf 'PASS sse_broadcast (a parked stream is reachable from another request)\n'

# the server was started with timeout: 1 -- a plain response this old would
# have been 504'd and the idle connection 408'd. The stream must outlive it.
sleep 2.2
poked="$(curl -s -m 5 "http://127.0.0.1:$port/poke")"
[ "$poked" = "poked 1" ] || fail "stream_timeout_exempt (after 2.2s the poke said '$poked')"
printf 'PASS stream_timeout_exempt (a parked stream outlives the request timeout)\n'

# disconnect the listener; the next emit must notice and reap
kill "$listener" 2>/dev/null || true
wait "$listener" 2>/dev/null || true
poked=""
for _ in $(seq 1 100); do
    poked="$(curl -s -m 5 "http://127.0.0.1:$port/poke")"
    [ "$poked" = "poked 0" ] && break; sleep 0.05
done
[ "$poked" = "poked 0" ] || fail "stream_disconnect (poke said '$poked')"
printf 'PASS stream_disconnect (emit returns false for a gone client, which is reaped)\n'
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- file responses: stream the bytes, never slurp them --------------------
head -c 5242880 /dev/urandom >"$scratch/blob.bin"
flog="$scratch/file.log"
timeout 120 ./gbasic --line-buffered tests/web_stream/file_server.bas "$scratch" >"$flog" 2>/dev/null &
fsrv=$!
port="$(wait_port "$flog" file_direct)"

curl -s -m 30 -D "$scratch/h1" -o "$scratch/got1" "http://127.0.0.1:$port/direct"
cmp -s "$scratch/blob.bin" "$scratch/got1" || fail "file_direct (bytes differ)"
grep -qi '^content-length: 5242880' "$scratch/h1" \
    || fail "file_direct (Content-Length: $(grep -i content-length "$scratch/h1" || echo missing))"
printf 'PASS file_direct (5MB file response byte-identical, length from fstat)\n'

curl -s -m 30 -o "$scratch/got2" "http://127.0.0.1:$port/blob.bin"
cmp -s "$scratch/blob.bin" "$scratch/got2" || fail "file_static (bytes differ)"
code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/nope.bin")"
[ "$code" = "404" ] || fail "file_static (missing file answered $code)"
printf 'PASS file_static (web.static serves the same bytes without slurping, 404 for missing)\n'
kill "$fsrv" 2>/dev/null || true
wait "$fsrv" 2>/dev/null || true

# --- draining a parked stream ----------------------------------------------
got="$scratch/drain.got"
timeout 90 ./gbasic tests/web_stream/stream_drain.bas >"$got" 2>&1 \
    || fail "stream_drain (driver exited nonzero: $(cat "$got"))"
diff -u tests/web_stream/stream_drain.out "$got" || fail "stream_drain (output diverged)"
printf 'PASS stream_drain (TERM ends a parked stream: client EOF, worker exits 0)\n'

# --- the negative: a stream response cannot carry a body -------------------
if ./gbasic tests/web_stream/negative_stream_body.bas >/dev/null 2>"$scratch/err1"; then
    fail "negative_stream_body (expected nonzero exit)"
fi
grep -q "write with webserver.emit, not a body" "$scratch/err1" \
    || fail "negative_stream_body (stderr: $(cat "$scratch/err1"))"
printf 'PASS negative_stream_body\n'

printf 'run_web_stream: all cases passed\n'
