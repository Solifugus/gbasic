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
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
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

printf 'TIER registration-time receive (finding 36)\n'
# A WATCHER BODY RUNS ONCE AT REGISTRATION, during `main`, before the event
# loop exists -- so a bare `receive()` there with nothing in the mailbox waits
# for a loop that has not started. Measured 2026-10-02: exit 124 under
# `timeout`, the `unwatch` on the next line unreachable, and NOTHING SAID.
# Reported by the gbasic-books session as finding 36 and deliberately NOT a
# widening of 2105, whose sentence ("receive() blocks the event loop") is false
# here because there is no loop yet.
#
# THE WARNING IS ISSUED WHERE THE RUNTIME KNOWS IT IS ABOUT TO BLOCK, not at
# the call, and the two controls below are what force that: the identical call
# with a reply already waiting is ordinary and must be silent, and so must the
# remedy the message names. A rule keyed on the SYNTAX would fire on both.
#
# THE PROGRAM STILL HANGS and that is correct rather than a shortfall: a live
# peer can send into this mailbox, since delivery is the kernel's job on the
# socket rather than the loop's, so this is a deadlock only if nothing will
# ever send -- which the runtime cannot know. A raise would break a program
# waiting on a slow peer. So the tier asserts the DIAGNOSTIC, and bounds the
# run, because a hang is not a failure and this suite already had to defend
# against one.
# `|| f36rc=$?` RATHER THAN A BARE CALL: this suite runs under `set -e` and the
# command under test is SUPPOSED to be killed, so a bare invocation aborts the
# script at the trap and the tier reports nothing -- which is the same trap
# run_http.sh records from the other direction and the reason that one prints no
# FAIL line when its pipeline matches nothing.
f36rc=0
timeout -k 5 20 ./gbasic --line-buffered tests/inbox_registration_block.bas \
    >"$work/f36.out" 2>"$work/f36.err" || f36rc=$?
if [ "$f36rc" != 124 ] && [ "$f36rc" != 137 ]; then
    printf '    exit %s, stdout:\n' "$f36rc"; cat "$work/f36.out"
    fail "the drained receive did not block -- this tier's premise is gone, not its claim"
elif ! grep -q '\[2110\]' "$work/f36.err"; then
    cat "$work/f36.err"; fail "a registration-time blocking receive warned nothing (finding 36)"
elif ! grep -q 'inbox_registration_block\.bas:[0-9]*:[0-9]*' "$work/f36.err"; then
    cat "$work/f36.err"; fail "2110 carries no location"
elif ! grep -q 'REGISTRATION' "$work/f36.err"; then
    cat "$work/f36.err"; fail "2110 does not say WHERE the program is, which is the whole point"
elif [ "$(grep -c '\[2110\]' "$work/f36.err")" != "1" ]; then
    cat "$work/f36.err"; fail "2110 fired more than once for one site"
else
    pass "warned once, located, and named the moment"
fi
# CONTROL 1: the same bare receive() with a reply ALREADY WAITING is the
# ordinary shape and must be silent. Without this, "it warns" is satisfied by a
# rule keyed on `receive()` appearing inside a `watch`.
if timeout -k 5 20 ./gbasic tests/inbox_registration_served.bas \
        >"$work/f36s.out" 2>"$work/f36s.err" &&
   ! grep -q '\[2110\]' "$work/f36s.err"; then
    pass "a receive() that gets a message is silent"
else
    cat "$work/f36s.err"; fail "2110 fires on a working program; that is noise"
fi
# CONTROL 2: the REMEDY must be silent, or the advice is unfollowable.
if timeout -k 5 20 ./gbasic tests/inbox_registration_timeout.bas \
        >"$work/f36t.out" 2>"$work/f36t.err" &&
   ! grep -q '\[2110\]' "$work/f36t.err"; then
    pass "a receive() with a timeout is silent"
else
    cat "$work/f36t.err"; fail "2110 fires on the remedy it names"
fi
# CONTROL 2b: A SLOW BUT LIVING PEER, which is the control that makes the
# predicate load-bearing and which the first three do NOT reach. They all
# return before the 250ms diagnostic slice expires, so a rule keyed on "it
# blocked" never gets asked -- measured: with `actor_no_sender_remains()`
# deleted, every other check in this tier stayed green. Here the watcher body
# blocks for 0.8s at registration while the child is alive and about to answer,
# so "it blocked" warns on a program with nothing wrong with it.
if timeout -k 5 20 ./gbasic tests/inbox_registration_slow.bas \
        >"$work/f36w.out" 2>"$work/f36w.err" &&
   ! grep -q '\[2110\]' "$work/f36w.err" &&
   grep -q 'got ' "$work/f36w.out"; then
    pass "a slow but LIVING peer is silent"
else
    cat "$work/f36w.err"; fail "2110 fires while a peer is still alive to answer"
fi
# CONTROL 3: outside a watcher entirely, a blocking receive is the whole point
# of `receive` and must say nothing. (inbox_receive.bas is already asserted
# silent above for 2105; asserted here for 2110 too, since they are different
# guards and a shared fixture proves only what each tier asks of it.)
if timeout -k 5 20 ./gbasic tests/inbox_receive.bas >/dev/null 2>"$work/f36o.err" &&
   ! grep -q '\[2110\]' "$work/f36o.err"; then
    pass "a sequential receive() is silent"
else
    cat "$work/f36o.err"; fail "2110 fires outside a watcher"
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
