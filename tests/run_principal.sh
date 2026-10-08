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
. tests/build_has.sh
if ! build_has actors; then
    printf '  SKIP actor tier (actors are not available on this platform)\n'
elif timeout -k 5 60 ./gbasic tests/principal_actor.bas >"$work/act.out" 2>"$work/act.err"; then
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
elif ! build_has listen; then
    printf '  SKIP handler tier (this platform cannot listen: webserver.listen is refused)\n'
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

printf 'TIER a watcher body acts for whoever made the WRITE\n'
# REPORTED BY THE gbasic-books SESSION and reproduced before anything was built:
# `bob` makes the write, the audit line says `alice`, and nothing is raised.
#
# A watcher body reads `principal()` out of the DYNAMIC scope, which is the
# writer's scope only while the body runs AT the write. A body QUEUED during an
# existing drain runs after that scope has been left, so it reported whoever
# happened to be in force when the queue drained. An audit trail attributing one
# person's write to another is worse than one that says nothing, and it was silent.
#
# THE REMEDY WAS CHOSEN BY MEASUREMENT, and the obvious one was wrong: refusing
# whenever `watcher_draining` is set cannot discriminate, because that flag is set
# for all four ways a body is reached and TWO of them are correct today --
# registration (the enclosing scope), the synchronous drain (the writer's
# principal, which is the ordinary audit watcher), the queued body (wrong), and the
# event loop (`nothing`, pinned by the HANDLER tier above). Refusing on it would
# have broken the case that works to fix the one that does not. So the write's
# principal is CARRIED with the queue entry instead.
#
# ASSERTED AS A DIFFERENCE between two writes in one program, which is what makes
# it more than "the audit says bob": the nested write is bob's and the outer one is
# alice's, and a build that carried the wrong scope reports alice for both.
cat >"$work/watch.bas" <<'BAS'
function who()
    ' `principal()` answers `nothing` when no block is open, and `nothing.name`
    ' raises -- which is the point of answering `nothing` rather than `{}`.
    ' Dynamically scoped, so calling it from here still sees the watcher's.
    p = principal()
    if p = nothing then
        return "nobody"
    end if
    return p.name
end function
program main( args )
    audit = []
    trigger = 0
    ledger = 0
    watch(ledger)
        append(audit, string(ledger) + "=" + who())
    end watch
    watch(trigger)
        ' A nested `with` inside a watcher body. This write is QUEUED, because a
        ' drain is already running, and its body runs after this block has exited.
        with principal({ name: "bob" })
            ledger = trigger * 100
        end with
    end watch
    with principal({ name: "alice" })
        trigger = 1
    end with
    print join(audit, " ")
end program
BAS
got="$({ timeout -k 5 60 ./gbasic "$work/watch.bas" 2>&1 </dev/null || true; })"
# `0=nobody` is the registration body (no principal yet); `100=bob` is the queued
# one, and it is bob because bob made that write.
if [ "$got" = "0=nobody 100=bob" ]; then
    pass "a queued body acts for the writer, not for whoever is draining"
else
    printf '    want: 0=nobody 100=bob\n    got:  %s\n' "$got"
    fail "a queued watcher body reports the wrong principal"
fi
# THE CONTROL, and it is the half that stops this becoming "watchers see nothing":
# the SYNCHRONOUS case must be unchanged, including that an unscoped write still
# reports nobody. Without it, a build where the capture always pushed `nothing`
# would pass the check above (the registration line) and lose the feature.
cat >"$work/watch2.bas" <<'BAS'
function who()
    ' `principal()` answers `nothing` when no block is open, and `nothing.name`
    ' raises -- which is the point of answering `nothing` rather than `{}`.
    ' Dynamically scoped, so calling it from here still sees the watcher's.
    p = principal()
    if p = nothing then
        return "nobody"
    end if
    return p.name
end function
program main( args )
    seen = []
    bal = 0
    watch(bal)
        append(seen, string(bal) + "=" + who())
    end watch
    with principal({ name: "carol" })
        bal = 1
    end with
    bal = 2
    print join(seen, " ")
end program
BAS
got2="$({ timeout -k 5 60 ./gbasic "$work/watch2.bas" 2>&1 </dev/null || true; })"
if [ "$got2" = "0=nobody 1=carol 2=nobody" ]; then
    pass "CONTROL: the synchronous case is unchanged, nobody included"
else
    printf '    want: 0=nobody 1=carol 2=nobody\n    got:  %s\n' "$got2"
    fail "the synchronous watcher case moved"
fi
# AND A CAPTURED PRINCIPAL IS LEFT WHEN THE BODY ENDS, like any other: a write
# AFTER the drain, outside every block, must report nobody rather than inheriting
# the entry the drain pushed. A missing pop is invisible in the two checks above.
cat >"$work/watch3.bas" <<'BAS'
function who()
    ' `principal()` answers `nothing` when no block is open, and `nothing.name`
    ' raises -- which is the point of answering `nothing` rather than `{}`.
    ' Dynamically scoped, so calling it from here still sees the watcher's.
    p = principal()
    if p = nothing then
        return "nobody"
    end if
    return p.name
end function
program main( args )
    seen = []
    t = 0
    led = 0
    watch(led)
        append(seen, string(led) + "=" + who())
    end watch
    watch(t)
        with principal({ name: "bob" })
            led = t * 100
        end with
    end watch
    with principal({ name: "alice" })
        t = 1
    end with
    print "after: " + who()
    led = 999
    print join(seen, " ")
end program
BAS
got3="$({ timeout -k 5 60 ./gbasic "$work/watch3.bas" 2>&1 </dev/null || true; })"
case "$got3" in
    *"after: nobody"*"999=nobody"*)
        pass "the captured principal is popped when the body ends" ;;
    *)
        printf '    want: after: nobody ... 999=nobody\n    got:  %s\n' "$got3"
        fail "a captured principal outlived its watcher body" ;;
esac

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
