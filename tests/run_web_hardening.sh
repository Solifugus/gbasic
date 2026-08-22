#!/usr/bin/env bash
# PLAT-WEB-3 hardening — the pieces §6 said "actually bite": request and
# idle-read timeouts (a slow client cannot hold a worker), and
# smuggling-resistant parsing (a framing this server does not speak is
# REFUSED, never guessed at; two Content-Lengths that disagree are an attack
# lever, not a formatting quirk). Plus web.trust_proxy, whose golden pins the
# rightmost-untrusted rule -- the correction to the draft's "first hop".
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP tests/web_hardening (curl not installed)\n'
    exit 0
fi

export GBASIC_PATH=stdlib
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fail() { printf 'FAIL %s\n' "$1"; exit 1; }

# --- trust_proxy: pure, golden ---------------------------------------------
if ./gbasic tests/web_hardening/trust_proxy_test.bas >"$scratch/tp.out" 2>&1 \
        && diff -u tests/web_hardening/trust_proxy_test.out "$scratch/tp.out"; then
    printf 'PASS trust_proxy (rightmost-untrusted, not the spoofable first hop)\n'
else
    fail "trust_proxy"
fi

# --- a server that never answers, timeout: 1 -------------------------------
log="$scratch/server.log"
timeout 60 ./gbasic --line-buffered tests/web_hardening/echo_server.bas >"$log" 2>/dev/null &
srv=$!
port=""
for _ in $(seq 1 100); do
    port="$(sed -n 's/^PORT //p' "$log" | head -1)"; [ -n "$port" ] && break; sleep 0.05
done
[ -n "$port" ] || fail "hardening (no port announced)"

code="$(curl -s -m 10 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$port/")"
[ "$code" = "504" ] || fail "timeout_504 (got $code)"
printf 'PASS timeout_504 (an unanswered request times out on the configured budget)\n'

code="$(curl -s -m 5 -o /dev/null -w '%{http_code}' -H 'Transfer-Encoding: chunked' \
        -X POST --data-binary x "http://127.0.0.1:$port/")"
[ "$code" = "501" ] || fail "smuggle_te (got $code)"
printf 'PASS smuggle_te (Transfer-Encoding is refused, not guessed at)\n'

line="$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 9\r\n\r\nhello' \
        | timeout 5 nc 127.0.0.1 "$port" | head -1 | tr -d '\r')"
case "$line" in *"400 Bad Request"*) ;; *) fail "smuggle_dual_cl (got '$line')";; esac
printf 'PASS smuggle_dual_cl (disagreeing Content-Lengths are refused)\n'

# agreeing duplicates are honest clients, not attacks: 504 here means the
# request PARSED and reached the (never-answering) app -- which is the pass
code="$(printf 'POST / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nhello' \
        | timeout 5 nc 127.0.0.1 "$port" | head -1 | tr -d '\r' | grep -o '[0-9][0-9][0-9]' | head -1)"
[ "$code" = "504" ] || fail "dup_cl_agreeing (got '$code')"
printf 'PASS dup_cl_agreeing (agreeing duplicates still parse)\n'

line="$( (sleep 3; printf '') | timeout 8 nc 127.0.0.1 "$port" | head -1 | tr -d '\r' )"
case "$line" in *"408 Request Timeout"*) ;; *) fail "timeout_408 (got '$line')";; esac
printf 'PASS timeout_408 (a silent connection is shed on the same budget)\n'

kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true
printf 'run_web_hardening: all cases passed\n'
