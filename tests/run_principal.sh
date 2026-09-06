#!/usr/bin/env bash
# `with principal(p)` and `principal()` -- the identity on whose behalf a body
# acts (docs/gbasic_ai_reference_and_primitives.md, step 2).
#
# The feature is small; what needs testing is the two ways it must NOT travel,
# because both are security properties and both are invisible in ordinary use.
#
# Tiers:
#   SEMANTICS  the self-checking fixture -- nothing-not-empty-record, nesting,
#              dynamic scope across calls, and unwinding on return/raise/goto
#   ACTOR      a principal does not cross `spawn`, WITH the control that an
#              explicit handoff works. Either half alone is satisfied by
#              something broken: "the child sees nothing" by a feature that
#              never works, "the child acts for gwen" by one that leaks.
#   HANDLER    a request handler fired from the event loop inherits NOTHING,
#              even though the listener was created inside a `with principal`
#              block -- the architecture's rule that a request is acted on for
#              whoever sent it. Control: the same handler establishing a
#              principal from the request header and acting under it.
#   REFUSAL    a non-record is refused, naming the kind; each beside its
#              nearest legal neighbour, including `with lock` which shares the
#              production and must be untouched
#   GRAMMAR    bison must still report zero conflicts. Not decoration: this
#              project rejected `IDENT expression` as a statement form over 4
#              MEASURED conflicts, and the whole reason `principal` rides the
#              `with lock` production is that recognising the opener by
#              POSITION costs nothing and reserves no word.
#   VALGRIND   a stack of Values popped across returns, raises and gotos
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics\n'
if timeout -k 5 60 ./gbasic tests/principal_test.bas >"$work/sem.out" 2>"$work/sem.err"; then
    checks="$(sed -n 's/^checks: //p' "$work/sem.out")"
    if ! grep -q '^mismatches: 0$' "$work/sem.out"; then
        grep '^MISMATCH' "$work/sem.out" || true
        fail "the fixture disagreed with itself"
    elif [ -z "$checks" ] || [ "$checks" -lt 17 ]; then
        # Coverage floor: a fixture that stopped running its checks also
        # reports zero mismatches.
        fail "only ${checks:-0} checks ran"
    else
        pass "$checks checks"
    fi
    if [ -s "$work/sem.err" ]; then
        cat "$work/sem.err"; fail "unexpected stderr"
    fi
else
    cat "$work/sem.err"; fail "the fixture did not run to completion"
fi

printf 'TIER a principal does not cross spawn, and an explicit handoff does\n'
if timeout -k 5 60 ./gbasic tests/principal_actor.bas >"$work/act.out" 2>"$work/act.err"; then
    got="$(tr '\n' '|' <"$work/act.out")"
    want='inherited:true|explicit:gwen|after:true|parent still: gwen|'
    if [ "$got" != "$want" ]; then
        printf '    got:  %s\n    want: %s\n' "$got" "$want"
        fail "actor isolation or the handoff moved"
    else
        pass "the child inherits nothing; handed the record it acts for gwen; the parent is unaffected"
    fi
else
    cat "$work/act.err"; fail "the actor fixture failed"
fi

printf 'TIER a request handler inherits nothing and acts for the caller\n'
hport="$(python3 - <<'PORT' 2>/dev/null || echo ""
import socket
s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()
PORT
)"
if [ -z "$hport" ] || ! command -v curl >/dev/null 2>&1; then
    printf '  SKIP handler tier (needs python3 and curl)\n'
else
    PORT="$hport" timeout -k 5 30 ./gbasic --line-buffered tests/principal_handler.bas \
        >"$work/h.out" 2>"$work/h.err" &
    hsrv=$!
    for _ in $(seq 1 60); do
        curl -s -m 1 -o /dev/null "http://127.0.0.1:$hport/" 2>/dev/null && break
        sleep 0.05
    done
    body="$(curl -s -m 5 -H "X-User: helen" "http://127.0.0.1:$hport/" || true)"
    sleep 0.3
    kill "$hsrv" 2>/dev/null || true
    wait "$hsrv" 2>/dev/null || true
    if [ "$body" != "inherited_nothing=true acting_for=helen" ]; then
        printf '    got: %s\n' "$body"
        cat "$work/h.err" || true
        fail "a handler must inherit no principal AND be able to act for the caller"
    elif ! grep -q '^handled, and after the block: true$' "$work/h.out"; then
        cat "$work/h.out"
        fail "the handler's own block was not left"
    else
        pass "listener bound inside \`with principal\`, handler saw none, acted for helen, left the block"
    fi
fi

printf 'TIER refusals, each beside its nearest legal neighbour\n'
refuse() { # body expected-fragment
    printf 'program main(args)\n%s\nend program\n' "$1" >"$work/r.bas"
    if ./gbasic "$work/r.bas" >/dev/null 2>"$work/r.err"; then
        fail "did NOT refuse: $2"
        return
    fi
    if grep -q "$2" "$work/r.err"; then
        pass "refused -> $2"
    else
        printf '    got: %s\n' "$(head -1 "$work/r.err")"
        fail "wrong message, wanted: $2"
    fi
}
accept() { # body label
    printf 'program main(args)\n%s\nend program\n' "$1" >"$work/a.bas"
    if ./gbasic "$work/a.bas" >/dev/null 2>"$work/a.err"; then
        pass "accepted -> $2"
    else
        printf '    %s\n' "$(head -1 "$work/a.err")"
        fail "must still be accepted: $2"
    fi
}
refuse '  with principal("alice")
    print(1)
  end with' 'with principal expects a record describing who is acting, not a string'
refuse '  with principal(42)
    print(1)
  end with' 'not a number'
refuse '  with principal(nothing)
    print(1)
  end with' 'not a nothing'
refuse '  with something(1)
    print(1)
  end with' 'expected lock or principal in a with block'
refuse '  print(principal(1))' 'principal expects no arguments'
# The controls. `with lock` shares this production and must be untouched, and
# a refusal suite with no control is satisfied by refusing everything.
accept '  f{file}= "/dev/null"
  with lock(f)
    print(1)
  end with' 'with lock, which shares the production'
accept '  with principal({ user: "ok" })
    print(principal().user)
  end with' 'a record principal'
accept '  print(string(principal() = nothing))' 'principal() outside any block'

printf 'TIER the grammar stayed at zero conflicts\n'
if command -v bison >/dev/null 2>&1; then
    if bison -d src/parser.y -o "$work/p.tab.c" 2>"$work/bison.err"; then
        if grep -qi "conflict" "$work/bison.err"; then
            fail "zero conflicts ($(grep -i conflict "$work/bison.err" | head -1))"
        else
            pass "zero shift/reduce conflicts; no word reserved"
        fi
    else
        fail "bison could not build the grammar"
    fi
else
    pass "zero conflicts (SKIP: no bison)"
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/principal_test.bas >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

if [ "$status" -ne 0 ]; then
    exit 1
fi
printf 'PASS tests/run_principal.sh\n'
