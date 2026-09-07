#!/usr/bin/env bash
# `mcp` -- publishing a toolset over the Model Context Protocol.
# Step 6 of docs/gbasic_ai_reference_and_primitives.md (§1.11).
#
# TWO TRANSPORTS, ONE DISPATCHER. `mcp.handle` turns a JSON-RPC request into a
# reply and does no I/O, so the stdio loop and an HTTP `server` block are thin
# wrappers over it. The PROTOCOL tier therefore needs neither transport, and
# the transport tiers test only what a transport can get wrong.
#
# Tiers:
#   PROTOCOL  the self-checking fixture. The distinction it exists for is
#             between a TOOL that failed (a result with isError -- the model
#             asked for something reasonable and can react) and a PROTOCOL
#             error (the client is malformed and cannot fix that by retrying).
#             Collapsing the two either way produces a well-formed reply that
#             means the wrong thing, so both are asserted.
#   STDIO     a real session down a pipe, the way a desktop client runs it.
#   DEADLOCK  THE SAME SESSION WITHOUT `--line-buffered`, which must TIME OUT.
#             This is a difference, not an assertion: block-buffered, the reply
#             sits in the pipe while the client waits for it and the server
#             waits for the client. Without this tier the flag looks like a
#             detail rather than the thing that makes the transport work.
#   HTTP      the same request over a `server` block must give a BYTE-IDENTICAL
#             reply to the stdio one -- which is what "one dispatcher" means,
#             and is not implied by both merely working.
#   CEILING   covered in the fixture: the mapped principal is the leak surface,
#             so a mapping wider than its declared ceiling refuses to start.
#   VALGRIND
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER the protocol, with no transport at all\n'
if timeout -k 5 120 ./gbasic tests/mcp_test.bas >"$work/p.out" 2>"$work/p.err"; then
    checks="$(sed -n 's/^checks: //p' "$work/p.out")"
    if ! grep -q '^mismatches: 0$' "$work/p.out"; then
        grep '^MISMATCH' "$work/p.out" || true
        fail "the fixture disagreed with itself"
    elif [ -z "$checks" ] || [ "$checks" -lt 39 ]; then
        fail "only ${checks:-0} checks ran"
    else
        pass "$checks checks"
    fi
    [ -s "$work/p.err" ] && { cat "$work/p.err"; fail "unexpected stderr"; } || true
else
    cat "$work/p.err"; fail "the fixture did not run to completion"
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP transport tiers (needs python3)\n'
else
    printf 'TIER a real stdio session\n'
    if timeout -k 5 90 python3 tests/mcp/client.py --line-buffered >"$work/s.out" 2>"$work/s.err"; then
        if grep -q '"protocolVersion": "2024-11-05"' "$work/s.out" &&
           grep -q '^tools: get_balance,whoami,boom$' "$work/s.out" &&
           grep -q '42.00 for a1' "$work/s.out" &&
           grep -q '"text": "alice"' "$work/s.out" &&
           grep -q '^exit: 0$' "$work/s.out"; then
            pass "initialize, tools/list, tools/call, the mapped principal, clean exit"
        else
            cat "$work/s.out" "$work/s.err"
            fail "the stdio session did not complete"
        fi
    else
        cat "$work/s.out" "$work/s.err"
        fail "the stdio client failed"
    fi

    printf 'TIER the same session WITHOUT --line-buffered deadlocks\n'
    # Demonstrated, not asserted. Every request must time out, and if any of
    # them answers then the flag is not doing what the transport needs it for
    # -- which would mean this whole tier had been proving nothing.
    if timeout -k 5 120 python3 tests/mcp/client.py none 2 >"$work/d.out" 2>"$work/d.err"; then
        answered=$(grep -c '"result"' "$work/d.out" || true)
        timeouts=$(grep -c 'TIMEOUT' "$work/d.out" || true)
        if [ "$answered" != "0" ]; then
            cat "$work/d.out"
            fail "block-buffered, $answered request(s) were answered; if the flag is not required then the stdio transport was never buffer-bound and this tier proves nothing"
        elif [ "$timeouts" -lt 3 ]; then
            cat "$work/d.out"
            fail "expected every request to time out, saw $timeouts"
        else
            pass "$timeouts requests timed out with no reply -- the flag is what makes the transport work"
        fi
    else
        cat "$work/d.out" "$work/d.err"
        fail "the deadlock probe did not finish"
    fi
fi

if ! command -v curl >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP http tier (needs curl and python3)\n'
else
    printf 'TIER the HTTP transport gives the identical reply\n'
    hport="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')"
    MCP_HTTP_PORT="$hport" timeout -k 5 60 ./gbasic --line-buffered tests/mcp/http_server.bas \
        >"$work/h.out" 2>"$work/h.err" &
    hsrv=$!
    req='{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"get_balance","arguments":{"id":"a1"}}}'
    up=0
    for _ in $(seq 1 300); do
        if curl -s -m 2 -X POST -d "$req" "http://127.0.0.1:$hport/rpc" -o "$work/http.reply" 2>/dev/null &&
           [ -s "$work/http.reply" ]; then
            up=1
            break
        fi
        sleep 0.05
    done
    kill "$hsrv" 2>/dev/null || true
    wait "$hsrv" 2>/dev/null || true
    # The same request answered by the stdio transport, for a byte comparison.
    printf '%s\n\n' "$req" | GBASIC_PATH=stdlib timeout -k 5 30 ./gbasic --line-buffered \
        tests/mcp/http_reference.bas >"$work/stdio.reply" 2>"$work/ref.err" || true
    if [ "$up" != "1" ]; then
        cat "$work/h.err" 2>/dev/null || true
        fail "the HTTP server never answered, so this tier tested nothing"
    elif [ ! -s "$work/stdio.reply" ]; then
        cat "$work/ref.err" 2>/dev/null || true
        fail "the stdio reference produced nothing to compare against"
    elif ! diff -u <(tr -d '\n' <"$work/stdio.reply") <(tr -d '\n' <"$work/http.reply") >"$work/diff" 2>&1; then
        cat "$work/diff"
        fail "the two transports gave DIFFERENT replies to one request; they are meant to share a dispatcher"
    else
        pass "the same request over HTTP and over stdio gives byte-identical JSON"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/mcp_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_mcp.sh\n'
