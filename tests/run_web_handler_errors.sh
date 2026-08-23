#!/usr/bin/env bash
# The response-shape error surface, reached the way a USER reaches it: from a
# handler inside a `server` block.
#
# `webserver_validate_response_value` has eighteen refusals. Two of them have
# negative-suite cases, and both drive the OLD path --
# `append(server.responses, {...})` -- where the raise is correct: the program
# made the mistake in its own frame and the error names that line. WEB-5
# pointed the same validator at a new caller without re-asking what its raises
# now mean. From a handler there is no such frame, `docs/reference.md` promises
# "a 500 named on stderr, and serving continues", and `workers: 1` -- the
# default -- has no supervisor to restart anything.
#
# So every case here asserts THREE things, and the third is the one no
# existing test makes:
#
#   1. the client gets 500
#   2. the reason is named on stderr
#   3. the server answers the NEXT request
#
# A failure of (3) reports which malformed response killed the listener.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP tests/web_handler_errors (curl not installed)\n'
    exit 0
fi

wait_port() { # logfile name
    local port="" i
    for i in $(seq 1 200); do
        port="$(sed -n 's/^PORT //p' "$1" | head -1)"
        [ -n "$port" ] && { printf '%s' "$port"; return 0; }
        sleep 0.05
    done
    fail "$2 (no port announced)"
}

log="$scratch/bad.log"
err="$scratch/bad.err"
timeout 120 ./gbasic --line-buffered tests/web_handler_errors/bad_responses.bas \
    >"$log" 2>"$err" &
srv=$!
port="$(wait_port "$log" bad_responses)"

# Every route, with the reason its refusal must name on stderr.
cases=(
    "body-type|webserver response body must be a string"
    "status-type|webserver response status must be an integer HTTP status"
    "status-range|webserver response status must be an integer HTTP status"
    "id-type|webserver response id must be a positive integer"
    "id-unknown|webserver response id does not match a pending request"
    "stream-type|webserver response stream must be true or false"
    "file-type|webserver response file must be a path string"
    "body-and-file|webserver response cannot carry both body and file"
    "stream-body|a stream:true response opens the connection"
    "headers-type|webserver response headers must be a record"
    "header-name|webserver response header name is invalid"
    "header-value-type|webserver response header values must be strings"
    "header-crlf|webserver response header value is invalid"
    "cookies-type|webserver response cookies must be an array"
    "cookie-type|webserver response cookie values must be strings"
    "cookie-crlf|webserver response cookie value is invalid"
    "not-a-record|not a response record"
)

# A sanity check before the matrix: the server is answering at all.
[ "$(curl -s -m 5 "http://127.0.0.1:$port/ok")" = "fine" ] \
    || fail "bad_responses (/ok did not answer before the matrix ran)"

for entry in "${cases[@]}"; do
    route="${entry%%|*}"
    want="${entry#*|}"

    code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' \
        "http://127.0.0.1:$port/$route" || printf 'dead')"
    [ "$code" = "500" ] \
        || fail "/$route (expected 500 on the wire, got $code)"

    # (3) -- the assertion the suite did not have. A malformed response is the
    # handler's mistake; it must not be the listener's death.
    alive="$(curl -s -m 5 "http://127.0.0.1:$port/ok" || printf '')"
    [ "$alive" = "fine" ] \
        || fail "/$route KILLED THE LISTENER (the next request went unanswered)"

    grep -qF "$want" "$err" \
        || fail "/$route (stderr never named the reason: $want)"

    printf 'PASS /%s (500, named, still serving)\n' "$route"
done

# The refusals must not be silent AND must not be the only thing on stderr --
# a listener that logged nothing would pass every check above by accident if
# the responses were being sent malformed instead of refused.
[ "$(grep -c 'refusing an invalid response' "$err")" -ge 16 ] \
    || fail "stderr (expected one refusal line per malformed record, got: $(grep -c 'refusing an invalid response' "$err"))"

kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- the adversarial crossing: a refusal a CLIENT can trigger ---------------
# Echoing a query parameter into a header is ordinary handler code. The CRLF
# refusal is the response-splitting control; surviving it is what stops the
# control from being a remote kill switch on the default `workers: 1`.
log="$scratch/echo.log"
err="$scratch/echo.err"
timeout 60 ./gbasic --line-buffered tests/web_handler_errors/echo_header.bas \
    >"$log" 2>"$err" &
srv=$!
port="$(wait_port "$log" echo_header)"

hdr="$(curl -s -m 5 -D - -o /dev/null "http://127.0.0.1:$port/echo?v=plain" \
    | tr -d '\r' | sed -n 's/^[Xx]-[Ee]cho: //p')"
[ "$hdr" = "plain" ] \
    || fail "echo_header (a benign value did not reach the header: got '$hdr')"
printf 'PASS echo_header/benign (the value is echoed)\n'

# %0d%0a -- the split attempt. Refused, and NOT on the wire as a second header.
raw="$(curl -s -m 5 -D - -o /dev/null "http://127.0.0.1:$port/echo?v=a%0d%0aevil:%201" || true)"
printf '%s' "$raw" | grep -qi '^evil:' \
    && fail "echo_header (RESPONSE SPLIT: an injected header reached the wire)"
printf '%s' "$raw" | grep -q '500' \
    || fail "echo_header (expected 500 for the CRLF value, got: $(printf '%s' "$raw" | head -1))"
[ "$(curl -s -m 5 "http://127.0.0.1:$port/ok")" = "fine" ] \
    || fail "echo_header (a CRLF query value KILLED THE LISTENER -- remote kill switch)"
printf 'PASS echo_header/crlf (refused, not split, still serving)\n'

kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

# --- the crossing the design's error model was never tested at: a raise
# --- inside a handler, under a supervisor that has to put it back ----------
log="$scratch/pool.log"
err="$scratch/pool.err"
timeout 120 ./gbasic --line-buffered tests/web_handler_errors/crash_pool.bas \
    >"$log" 2>"$err" &
srv=$!
port="$(wait_port "$log" crash_pool)"

[ "$(curl -s -m 5 "http://127.0.0.1:$port/ok")" = "fine" ] \
    || fail "crash_pool (/ok did not answer before the crash)"

# Crash MORE workers than the pool has. Killing one proves nothing -- the
# survivor answers /ok instantly and a supervisor that respawns nothing looks
# identical. Outliving the pool size is what requires a replacement to have
# been made, and it is exactly where an unsupervised pool runs out of workers
# and the service goes dark for good.
for _ in 1 2 3 4; do
    curl -s -m 5 -o /dev/null "http://127.0.0.1:$port/crash" 2>/dev/null || true
    sleep 0.8
done

grep -qF "worker died" "$err" \
    || fail "crash_pool (the supervisor never reported a death: $(cat "$err"))"

# The service must still be there, on workers that only exist because they
# were put back.
alive=""
for _ in $(seq 1 50); do
    if [ "$(curl -s -m 5 "http://127.0.0.1:$port/ok" 2>/dev/null || printf '')" = "fine" ]; then
        alive=yes
        break
    fi
    sleep 0.2
done
[ -n "$alive" ] \
    || fail "crash_pool (the pool ran out of workers -- nothing was respawned)"
printf 'PASS crash_pool (let-it-crash %d times, and the pool puts them back)\n' 4

kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true

printf 'run_web_handler_errors: %d response cases + 3 crossings passed\n' "${#cases[@]}"
