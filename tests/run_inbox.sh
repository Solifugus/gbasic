#!/usr/bin/env bash
# `watch(inbox.messages)` -- an actor reply delivered by the event loop.
#
# The platform half of the tool pool (docs/gbasic_ai_reference_and_primitives.md
# step 3). `receive()` blocks, which is fatal in a handler, so the interpreter's
# one inbox joins the loop's `poll` set the way an http transfer does.
#
# Tiers:
#   DELIVERY   five replies arrive after `main` returns with NO server bound,
#              each exactly once, carrying the worker's own answer
#   UNWATCH    a mailbox has no completion, so the program says when it is
#              finished; the fixture terminating at all is the assertion, which
#              is why it is bounded -- the first version never exited, and a
#              hang is not a failure
#   NO_WATCH   the control: actors and a reply in flight but no watcher, and
#              the program must exit at once. Without it, "the loop runs after
#              main" would be true of every program rather than a watching one
#   RECEIVE    the control on the platform change: the blocking path still works
#   WARN       `receive()` inside a watcher warns and still answers; outside it
#              is silent
#   VALGRIND
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER delivery, and unwatch ends it\n'
if timeout -k 5 60 ./gbasic --line-buffered tests/inbox_test.bas \
        >"$work/d.out" 2>"$work/d.err"; then
    if ! grep -q '^delivered: 5$' "$work/d.out"; then
        cat "$work/d.out"; fail "not every reply was delivered"
    elif ! grep -q '^every reply arrived exactly once: true$' "$work/d.out"; then
        cat "$work/d.out"; fail "a reply was delivered more than once"
    elif ! grep -q "^and carried the worker's own answer: true$" "$work/d.out"; then
        cat "$work/d.out"; fail "a delivered message did not carry what the worker sent"
    else
        pass "5 replies after main returned, each exactly once, and unwatch ended the loop"
    fi
    [ -s "$work/d.err" ] && { cat "$work/d.err"; fail "unexpected stderr"; } || true
else
    st=$?
    if [ "$st" = "124" ] || [ "$st" = "137" ]; then
        fail "the program HUNG: with no completion of its own, a mailbox needs unwatch to end the loop"
    else
        cat "$work/d.err"; fail "exit $st"
    fi
fi

printf 'TIER the control: no watcher, no loop\n'
start=$(date +%s%N)
if timeout -k 5 30 ./gbasic tests/inbox_no_watch.bas >"$work/n.out" 2>"$work/n.err"; then
    ms=$(( ($(date +%s%N) - start) / 1000000 ))
    if [ "$ms" -gt 1500 ]; then
        fail "took ${ms}ms; with no watcher an outstanding reply must not hold the program open"
    else
        pass "${ms}ms, the 2s reply abandoned"
    fi
else
    cat "$work/n.err"; fail "the control program failed"
fi

printf 'TIER the control: receive() is unchanged\n'
if timeout -k 5 30 ./gbasic tests/inbox_receive.bas >"$work/r.out" 2>"$work/r.err"; then
    if grep -q '^from the worker$' "$work/r.out" &&
       grep -q '^receive still blocks and still returns$' "$work/r.out"; then
        pass "the blocking path still delivers"
    else
        cat "$work/r.out"; fail "receive() moved"
    fi
else
    cat "$work/r.err"; fail "the blocking fixture failed"
fi

printf 'TIER receive() inside a watcher warns, and still answers\n'
if ! command -v curl >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP warn tier (needs curl and python3)\n'
else
    wport="$(python3 - <<'PORT'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PORT
)"
    PORT="$wport" timeout -k 5 60 ./gbasic --line-buffered tests/inbox_warn.bas \
        >"$work/w.out" 2>"$work/w.err" &
    wsrv=$!
    # Generous, and the outcome is RECORDED: a server that never came up must
    # be reported as that and not as "it did not warn". Under a loaded machine
    # this tier once blamed the warning for a listener that had not finished
    # binding, which is the wrong cause named confidently.
    up=0
    for _ in $(seq 1 300); do
        if curl -s -m 1 -o /dev/null "http://127.0.0.1:$wport/" 2>/dev/null; then
            up=1
            break
        fi
        sleep 0.05
    done
    body="$(curl -s -m 20 "http://127.0.0.1:$wport/" || true)"
    sleep 0.2
    kill "$wsrv" 2>/dev/null || true
    wait "$wsrv" 2>/dev/null || true
    if [ "$up" != "1" ]; then
        cat "$work/w.err" 2>/dev/null || true
        fail "the fixture server never answered, so this tier tested nothing (a machine problem, not a warning problem)"
    elif ! grep -q 'warning: receive() blocks the event loop' "$work/w.err"; then
        cat "$work/w.err"; fail "a blocking receive inside a watcher did not warn"
    elif ! grep -q 'inbox_warn\.bas:[0-9]*:[0-9]*' "$work/w.err"; then
        cat "$work/w.err"; fail "the warning carries no location"
    elif [ "$body" != "worker replied" ]; then
        printf '    got: %s\n' "$body"
        fail "it warned and CHANGED THE ANSWER; this is a warning, not a refusal"
    else
        pass "warned, located, and the handler still answered"
    fi
    # The control: the same receive() outside the loop must be silent.
    if timeout -k 5 30 ./gbasic tests/inbox_receive.bas >/dev/null 2>"$work/rq.err" &&
       [ ! -s "$work/rq.err" ]; then
        pass "an ordinary receive() is silent"
    else
        cat "$work/rq.err"; fail "receive() warns outside the loop too; that is noise"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/inbox_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_inbox.sh\n'
