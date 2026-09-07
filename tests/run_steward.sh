#!/usr/bin/env bash
# Steward -- the reference application's first slice, and the SKELETON the
# proposal's Part 3 item 13 asked for before more phase documents were written.
# Step 8 of docs/gbasic_ai_reference_and_primitives.md.
#
# WHAT IT IS FOR. Every layer under it has its own suite, and each passes. This
# is the only thing that exercises them JOINED -- an HTTP request becoming a
# run, a run becoming a model call in a worker, a reply becoming a tool call, an
# approval arriving minutes later on a different request, and a conversation
# ending. Both defects it found were invisible to every layer's own tests:
#
#   * `if req.headers["x-user"] = ""` LET AN UNAUTHENTICATED REQUEST THROUGH. A
#     missing header reads back as `unknown`, and `unknown = ""` is FALSE. The
#     skeleton answered 202 to a request with no identity at all. That is the
#     shape of a real authentication bypass, and the AUTH tier exists for it.
#   * A top-level `watch` REGISTERS NOTHING when a `program` block exists, and
#     says nothing about it -- so the pool's replies were never delivered and
#     every run sat in `calling_model` forever.
#
# Tiers:
#   CHAIN     three conversations end to end: a read-only tool, an approved
#             mutating one, and a REFUSED one -- which must end with the model
#             saying so rather than with an error, because a person declining
#             one action is an answer.
#   PARKED    two runs suspended AT ONCE while the server keeps serving, then
#             resumed by separate later requests. This is §1.8's whole reason,
#             and it only works because the run is text in a store between
#             steps -- `encode` on the way in and `decode` on the way out, on
#             the ordinary path rather than in a unit test.
#   AUTH      no identity, and empty identity, are both refused.
#   MISSING   an unknown run is 404 on both routes.
#
# No valgrind tier: every library beneath this has one, and what is under test
# here is the joining rather than any of them. Needs python3 and a replay
# fixture set; no network.
set -euo pipefail

cd "$(dirname "$0")/.."
make >/dev/null
export GBASIC_PATH=stdlib

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP tests/run_steward.sh (needs python3)\n'
    exit 0
fi

work="$(mktemp -d)"
srv=""
cleanup() {
    [ -n "$srv" ] && kill "$srv" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

port="$(python3 -c 'import socket;s=socket.socket();s.bind(("127.0.0.1",0));print(s.getsockname()[1]);s.close()')"
STEWARD_PORT="$port" STEWARD_REPLAY=examples/steward/replay \
    timeout -k 5 120 ./gbasic --line-buffered examples/steward/steward.bas \
    >"$work/srv.out" 2>"$work/srv.err" &
srv=$!
up=0
for _ in $(seq 1 300); do
    if curl -s -m 2 -o /dev/null "http://127.0.0.1:$port/run/probe" 2>/dev/null; then
        up=1
        break
    fi
    sleep 0.05
done
if [ "$up" != "1" ]; then
    cat "$work/srv.err" 2>/dev/null || true
    fail "the application never answered, so nothing here was tested"
    exit 1
fi

if ! timeout -k 5 90 python3 tests/steward/drive.py "$port" >"$work/d.out" 2>"$work/d.err"; then
    cat "$work/d.out" "$work/d.err" "$work/srv.err"
    fail "the driver did not finish"
    exit 1
fi
kill "$srv" 2>/dev/null || true
wait "$srv" 2>/dev/null || true
srv=""

got() { sed -n "s/^$1: //p" "$work/d.out"; }

printf 'TIER two runs suspended at once, while the server keeps serving\n'
if [ "$(got parked_b)" = "awaiting_approval" ] && [ "$(got parked_c)" = "awaiting_approval" ]; then
    if [ "$(got early_a)" = "done" ]; then
        pass "two conversations parked awaiting a person while a third ran to completion"
    else
        cat "$work/d.out"
        fail "the read-only conversation did not complete while the others were parked"
    fi
else
    cat "$work/d.out" "$work/srv.err"
    fail "a mutating tool must park the run: got b=$(got parked_b) c=$(got parked_c)"
fi

printf 'TIER three conversations, end to end\n'
if [ "$(got a)" != "done|Your balance is 42.00." ]; then
    cat "$work/d.out" "$work/srv.err"; fail "read-only path: $(got a)"
elif [ "$(got b)" != "done|Card c9 is frozen." ]; then
    cat "$work/d.out" "$work/srv.err"; fail "approved path: $(got b)"
elif [ "$(got c)" != "done|Understood -- I have not frozen the card." ]; then
    # A refusal is an ANSWER: the model is told in the transcript and says so.
    cat "$work/d.out" "$work/srv.err"; fail "refused path: $(got c)"
else
    pass "read-only completed, approval granted froze the card, approval refused ended with the model saying so"
fi

printf 'TIER identity is required\n'
if [ "$(got no_identity)" != "401" ]; then
    fail "a request with NO identity got $(got no_identity), not 401 -- an unauthenticated run was created"
elif [ "$(got empty_identity)" != "401" ]; then
    fail "a request with an EMPTY identity got $(got empty_identity), not 401"
else
    pass "a request with no identity, and one with an empty identity, are both refused"
fi

printf 'TIER an unknown run is not found\n'
if [ "$(got unknown_run)" = "404" ] && [ "$(got approve_unknown)" = "404" ]; then
    pass "both routes answer 404 rather than inventing a run"
else
    fail "unknown run: read=$(got unknown_run) approve=$(got approve_unknown)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_steward.sh\n'
