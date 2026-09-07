#!/usr/bin/env bash
# `tools` -- one declaration of what a model may call, and the safe way to call
# it (docs/gbasic_ai_reference_and_primitives.md, step 3).
#
# Tiers:
#   SEMANTICS  the self-checking fixture: declaration, validation, defaults,
#              effects, the principal reaching the body -- and the ORACLE,
#              which drives dispatch FROM `tools.schema`'s output so that what
#              the model is TOLD and what is ENFORCED cannot drift. That is the
#              whole reason this layer exists: `llm.tool` takes a hand-written
#              JSON schema AND a function with nothing checking they agree.
#   SURVIVES   a tool that RAISES comes back as a result and the next call still
#              works. Asserted in the fixture; measured against the older path,
#              where a raising tool still ends the entire run.
#   REFUSAL    17 define-time refusals, each beside its nearest legal
#              neighbour, since a refusal suite with no control is satisfied by
#              refusing everything.
#   POOL       three calls across three workers: the FAST one must come back
#              first though it was sent last, which is what says the bodies ran
#              in parallel; plus the principal handoff, which can only reach a
#              worker by being written into the message.
#   HANDLER    the whole chain -- a request handler that calls a tool and does
#              not block. Asserted as an ORDERING (both handlers start before
#              either answer), not a clock.
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

printf 'TIER semantics and the schema/dispatch oracle\n'
if timeout -k 5 120 ./gbasic tests/tools_test.bas >"$work/sem.out" 2>"$work/sem.err"; then
    checks="$(sed -n 's/^checks: //p' "$work/sem.out")"
    if ! grep -q '^mismatches: 0$' "$work/sem.out"; then
        grep '^MISMATCH' "$work/sem.out" || true
        fail "the fixture disagreed with itself"
    elif [ -z "$checks" ] || [ "$checks" -lt 40 ]; then
        fail "only ${checks:-0} checks ran"
    else
        pass "$checks checks, including the oracle driven from tools.schema"
    fi
    [ -s "$work/sem.err" ] && { cat "$work/sem.err"; fail "unexpected stderr"; } || true
else
    cat "$work/sem.err"; fail "the fixture did not run to completion"
fi

printf 'TIER define-time refusals, each beside its nearest legal neighbour\n'
tool_prog() { # body -> a program that defines a toolset
    cat >"$work/d.bas" <<EOF
load tools
function f(args)
    return 1
end function
$1
EOF
}
refuse() { # define-body expected-fragment
    tool_prog "$1"
    if ./gbasic "$work/d.bas" >/dev/null 2>"$work/d.err"; then
        fail "did NOT refuse: $2"
        return
    fi
    if grep -q "$2" "$work/d.err"; then
        pass "refused -> $2"
    else
        printf '    got: %s\n' "$(head -1 "$work/d.err")"
        fail "wrong message, wanted: $2"
    fi
}
accept() { # define-body label
    tool_prog "$1"
    if ./gbasic "$work/d.bas" >/dev/null 2>"$work/d.err"; then
        pass "accepted -> $2"
    else
        printf '    %s\n' "$(head -1 "$work/d.err")"
        fail "must still be accepted: $2"
    fi
}
OK_PARAMS='params: [ { name: "a", type: "number", describe: "an a", required: true } ]'

refuse 'ts = tools.define("t", [ 7 ])' 'every entry must be a record'
refuse 'ts = tools.define("t", "not an array")' 'define expects an array'
refuse 'ts = tools.define("t", [ { describe: "d", params: [], reads: [], mutates: [], fn: f } ])' 'every entry needs a string name'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [], reads: [], mutates: [], fn: f }, { name: "a", describe: "e", params: [], reads: [], mutates: [], fn: f } ])' "'a' is defined twice"
refuse 'ts = tools.define("t", [ { name: "a", params: [], reads: [], mutates: [], fn: f } ])' 'needs a describe'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [], reads: [], mutates: [], fn: 5 } ])' 'needs fn to be a function value'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: "no", reads: [], mutates: [], fn: f } ])' 'needs params as an array'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", describe: "d", required: true } ], reads: [], mutates: [], fn: f } ])' "parameter 'p' of tool 'a' needs a type"
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "int", describe: "d", required: true } ], reads: [], mutates: [], fn: f } ])' "unknown type 'int'"
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d", required: true }, { name: "p", type: "string", describe: "d", required: false } ], reads: [], mutates: [], fn: f } ])' "declares parameter 'p' twice"
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", required: true } ], reads: [], mutates: [], fn: f } ])' "needs a describe"
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d" } ], reads: [], mutates: [], fn: f } ])' 'must say required: true or false'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d", required: true, default: 1 } ], reads: [], mutates: [], fn: f } ])' 'is required and also has a default'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d", required: false, default: "x" } ], reads: [], mutates: [], fn: f } ])' 'is not a number'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", '"$OK_PARAMS"', mutates: [], fn: f } ])' 'needs reads as an array'
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", '"$OK_PARAMS"', reads: [], fn: f } ])' 'needs mutates as an array'
# Unknown fields refused BY NAME, on an entry and on a parameter. A misspelled
# `describe` is otherwise indistinguishable from a deliberate omission.
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", descripton: "typo", params: [], reads: [], mutates: [], fn: f } ])' "a tool entry has no field 'descripton'"
refuse 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d", required: false, dfault: 1 } ], reads: [], mutates: [], fn: f } ])' "has no field 'dfault'"

accept 'ts = tools.define("t", [ { name: "a", describe: "d", params: [], reads: [], mutates: [], fn: f } ])' 'a tool with no parameters'
accept 'ts = tools.define("t", [ { name: "a", describe: "d", '"$OK_PARAMS"', reads: ["db"], mutates: ["db"], fn: f } ])' 'a tool that declares what it changes'
accept 'ts = tools.define("t", [ { name: "a", describe: "d", params: [ { name: "p", type: "number", describe: "d", required: false, default: 3 } ], reads: [], mutates: [], fn: f } ])' 'an optional parameter with a default of the right type'

printf 'TIER the pool: parallel bodies and the principal handoff\n'
if timeout -k 5 90 ./gbasic --line-buffered tests/tools/pool.bas >"$work/p.out" 2>"$work/p.err"; then
    order="$(sed -n 's/^reply \(c[0-9]*\).*/\1/p' "$work/p.out" | tr '\n' ' ')"
    first="$(echo "$order" | awk '{print $1}')"
    n="$(printf '%s\n' $order | grep -c . || true)"
    if [ "$n" != "3" ]; then
        cat "$work/p.out"; fail "$n of 3 replies arrived"
    elif [ "$first" != "c3" ]; then
        # c3 has no sleep and was sent LAST. If it is not first, the bodies ran
        # one after another and the pool is a queue, not a pool.
        cat "$work/p.out"
        fail "replies came back in [$order]; the call with no sleep was sent last and must return first"
    elif ! grep -q '^reply c3: iris ' "$work/p.out"; then
        cat "$work/p.out"; fail "the principal did not reach the worker's tool body"
    else
        pass "3 replies in [$order]; the fast call returned first; the worker acted for iris"
    fi
    [ -s "$work/p.err" ] && { cat "$work/p.err"; fail "unexpected stderr"; } || true
else
    cat "$work/p.err"; fail "the pool fixture did not finish"
fi

printf 'TIER the whole chain: a handler that calls a tool without blocking\n'
if ! command -v curl >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP handler tier (needs curl and python3)\n'
else
    hport="$(python3 - <<'PORT'
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PORT
)"
    PORT="$hport" timeout -k 5 60 ./gbasic --line-buffered tests/tools/pool_server.bas \
        >"$work/h.out" 2>"$work/h.err" &
    hsrv=$!
    up=0
    for _ in $(seq 1 300); do
        if curl -s -m 1 -o /dev/null "http://127.0.0.1:$hport/?q=probe" 2>/dev/null; then
            up=1
            break
        fi
        sleep 0.05
    done
    if [ "$up" != "1" ]; then
        cat "$work/h.err" 2>/dev/null || true
        kill "$hsrv" 2>/dev/null || true
        fail "the pooled server never answered, so this tier tested nothing"
    fi
    : >"$work/bodies"
    ( curl -s -m 20 -H "X-User: a" "http://127.0.0.1:$hport/?q=one" >>"$work/bodies"; echo >>"$work/bodies" ) &
    c1=$!
    ( curl -s -m 20 -H "X-User: b" "http://127.0.0.1:$hport/?q=two" >>"$work/bodies"; echo >>"$work/bodies" ) &
    c2=$!
    wait $c1 $c2 2>/dev/null || true
    sleep 0.3
    kill "$hsrv" 2>/dev/null || true
    wait "$hsrv" 2>/dev/null || true
    # The probe request above is answered too, so filter to the two we care
    # about and assert the ORDERING of the pair.
    seq_line="$(grep -E '^(started|answered) ' "$work/h.out" | tail -4 | awk '{print $1}' | tr '\n' ' ' || true)"
    got_one=$(grep -c 'answer for one' "$work/bodies" || true)
    got_two=$(grep -c 'answer for two' "$work/bodies" || true)
    if [ "$got_one" != "1" ] || [ "$got_two" != "1" ]; then
        cat "$work/bodies" "$work/h.out" "$work/h.err"
        fail "both clients must receive their own tool result"
    elif [ "$seq_line" != "started started answered answered " ]; then
        cat "$work/h.out"
        fail "sequence was [$seq_line]; both handlers must return before either answer, or the loop was blocked"
    else
        pass "two handlers returned unanswered; both tool results arrived through the inbox and were sent"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/tools_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

[ "$status" -ne 0 ] && exit 1
printf 'PASS tests/run_tools.sh\n'
