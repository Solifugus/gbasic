#!/usr/bin/env bash
# `agent` -- a conversation in progress is a VALUE, and the loop is a pure step.
# Step 5 of docs/gbasic_ai_reference_and_primitives.md (§1.8).
#
# WHY IT IS NOT A LOOP. The naive design sends the transcript, takes the reply,
# dispatches a tool call and re-sends. The approval gate makes that
# unwritable: a person has to say yes, they may take a minute, the wait spans
# HTTP requests, and the server is single-threaded and watcher-driven. Nothing
# may block. So the conversation is a record and `agent.apply` is a pure
# function of (run, event) returning the new run plus the ACTIONS its caller
# must perform.
#
# Tiers:
#   SEMANTICS  the self-checking fixture. The load-bearing part is STORAGE: a
#              run must survive encode/decode and behave identically, because
#              that is the only thing that makes an approval arriving later on
#              another request possible. Asserted as a DIFFERENCE between the
#              original and the revived run, since "it encodes" alone is
#              satisfied by a run that encodes and then steps differently.
#              Two structural facts fall out and are asserted: a TOOLSET
#              cannot be in the run (function values; encode refuses them) so
#              the run holds tools.schema, and `expires_at` is a NUMBER because
#              encode refuses a datetime too.
#   PURITY     asserted twice over, and it is what the design rests on: the
#              same (run, event) gives the same actions, and stepping does not
#              mutate the run it was given.
#   GATE       a mutating tool ASKS and a read-only one does not -- a
#              difference, since asserting only the first is satisfied by a run
#              that asks permission for everything. Its control is a context
#              that turns the gate off, which is what says the gate is a
#              decision rather than a hard rule.
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

printf 'TIER the run is data, the step is pure, and the gate is a difference\n'
if timeout -k 5 120 ./gbasic tests/agent_test.bas >"$work/out" 2>"$work/err"; then
    checks="$(sed -n 's/^checks: //p' "$work/out")"
    if ! grep -q '^mismatches: 0$' "$work/out"; then
        grep '^MISMATCH' "$work/out" || true
        fail "the fixture disagreed with itself"
    elif [ -z "$checks" ] || [ "$checks" -lt 45 ]; then
        fail "only ${checks:-0} checks ran"
    else
        pass "$checks checks"
    fi
    [ -s "$work/err" ] && { cat "$work/err"; fail "unexpected stderr"; } || true
else
    cat "$work/err"; fail "the fixture did not run to completion"
fi

printf 'TIER `load agent` alone works\n'
# A library must load its own dependencies. Written without them every fixture
# still passed, because each one also loaded `llm` and `tools` for its own use
# -- so no test could tell the difference. This one loads nothing else.
if timeout -k 5 30 ./gbasic tests/agent_alone.bas >"$work/alone.out" 2>"$work/alone.err"; then
    if grep -q '^stage calling_model$' "$work/alone.out" &&
       grep -q '^action start_model$' "$work/alone.out"; then
        pass "a program that loads only agent gets a working agent"
    else
        cat "$work/alone.out"; fail "load agent alone did not produce a working run"
    fi
else
    cat "$work/alone.err"
    fail "load agent alone failed -- the library is not loading its own dependencies"
fi

printf 'TIER the run performs no I/O\n'
# `apply` must read no clock, open no socket and touch no file. A structural
# check rather than a behavioural one: the fixture cannot notice a clock read
# that happens to return the same value twice, and a run that read the time
# would resume differently after a restart -- which is exactly what §1.8 needs
# not to happen.
body="$(sed -n '/function apply(run, event)/,/^    end function/p' stdlib/agent.bas)"
if [ -z "$body" ]; then
    fail "could not read agent.apply out of stdlib/agent.bas -- it moved"
else
    banned=""
    for name in "now(" "epoch(" "sleep(" "read(" "write(" "random_bytes(" "webclient." "http." "pg." "sqlite."; do
        if printf '%s' "$body" | grep -q -- "$name"; then
            banned="$banned $name"
        fi
    done
    if [ -n "$banned" ]; then
        fail "agent.apply reaches for I/O or a clock:$banned"
    else
        pass "no clock, no id generation, no file, no socket in the step"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/agent_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_agent.sh\n'
