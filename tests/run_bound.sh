#!/usr/bin/env bash
# `bound(fn, context)` -- a function value that carries an EXPLICIT context.
#
# WHAT IT IS FOR. gBASIC has no closures, deliberately: a captured environment
# admits reference cycles in a refcounted runtime and cannot cross `spawn`'s
# fork+exec, and `encode` totality is what lets an agent run sit in a store
# between HTTP requests. The cost was that a callback could not carry state,
# and MEASURED, that cost two workarounds in this tree:
#
#   examples/automation_lab/08_what_price.bas  "the 10.00 is written in", so
#       pricing a second product means a second identical function
#   stdlib/datagrid.bas  the GTK handler reaches its grid through a
#       program-global `_DATAGRID` registry, which a LIBRARY cannot create --
#       so the workaround leaked into the API of every program using it
#
# NOT A CLOSURE, and that is why it is affordable: a closure captures an
# ENVIRONMENT implicitly and by reference; this captures ONE NAMED VALUE, by
# copy. A gBASIC record is a value, so nothing cyclic is constructible.
#
# THE CONTEXT GOES LAST, so one function serves bound and unbound use given a
# literal default (`function f(x, ctx = nothing)`). Context-first would make
# the two signatures incompatible. `webserver.on_request` passes its own
# context FIRST and is the one bespoke precedent, left alone.
#
# TIER 3 IS THE LOAD-BEARING ONE AND IT EXISTS BECAUSE `bound` INTRODUCED A
# SILENT FAILURE. Several paths keep only a function's NAME, and measured
# before the guard, `gi.connect(obj, sig, bound(h, ctx))` ran the handler with
# the context SILENTLY GONE -- `ctx = nothing`, no error, exit 0. The refusal
# ships WITH the feature rather than after somebody loses a context to it.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT

echo "--- TIER 1: semantics, refusals and controls ---"
out="$({ ./gbasic tests/bound/bound_test.bas 2>&1 </dev/null || true; })"
if printf '%s\n' "$out" | grep -q '^MISMATCH'; then
    printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
    echo "FAIL semantics"; status=1
else
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    # A COVERAGE FLOOR: a fixture whose checks stopped running reports no
    # MISMATCH either, which this tree has been bitten by more than once.
    if [ "${ok:-0}" -lt 16 ]; then
        echo "FAIL semantics (only $ok checks ran; at least 16 expected)"; status=1
    else
        echo "PASS semantics ($ok checks)"
    fi
fi

echo "--- TIER 2: the generality claim, against the REAL library ---"
# decision.quantity was written BEFORE `bound` existed and calls its model as
# f(b). It must need NO change -- that is the whole argument for putting the
# context in the VALUE rather than adding a parameter to every callback API.
cat > "$tmp/real.bas" <<'BAS'
load decision
function price_star(b, ctx)
    if b >= 0 - 1 then
        return unknown
    end if
    return ctx.cost * b / (1 + b)
end function
program main( args )
    b = 0 - 2.0
    for each c in [10.0, 22.5]
        r = decision.quantity({ parameter: { estimate: b, low: b - 0.2, high: b + 0.2 },
                                map: bound(price_star, { cost: c }),
                                model: "constant-elasticity profit maximum" })
        if r.defined then
            print "cost " + string(c) + " price " + string(round(r.recommended, 2))
        else
            print "cost " + string(c) + " REFUSED"
        end if
    next
end program
BAS
got="$({ GBASIC_PATH=stdlib ./gbasic "$tmp/real.bas" 2>&1 </dev/null || true; })"
# ASSERTED AS A DIFFERENCE: two costs must give two prices, or "it ran" is
# satisfied by a mechanism that accepts a context and ignores it.
if printf '%s\n' "$got" | grep -q "cost 10 price 20" && \
   printf '%s\n' "$got" | grep -q "cost 22.5 price 45"; then
    echo "PASS generality (an unchanged library, two contexts, two answers)"
else
    printf '%s\n' "$got" | sed 's/^/  /'
    echo "FAIL generality"; status=1
fi

echo "--- TIER 3: refused where a context cannot travel, never dropped ---"
say() {
    printf '%s\n' "$1" >"$tmp/p.bas"
    { GBASIC_PATH=stdlib ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; } \
        | head -1 | sed 's/^[^:]*:[0-9]*:[0-9]*: //'
}
want() {
    local got; got="$(say "$2")"
    case "$got" in
        *"$3"*) printf 'ok   %s\n' "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
# THE PROBE IS A REAL FILE, not /dev/stdin. Written the latter way it reported
# "gi unavailable" on a machine where gi plainly works, SKIPPING the tier that
# carries this suite -- and a skip reads like a pass in scroll-back, which is
# the way run_all.sh says a gate goes quiet.
printf 'load gi\nprogram main(args)\nend program\n' > "$tmp/gi_probe.bas"
if GBASIC_PATH=stdlib ./gbasic "$tmp/gi_probe.bas" >/dev/null 2>&1; then
    want 'gi.connect refuses a bound function' 'load gi
gi.require("Gio", "2.0")
c = gi.new("Gio.Cancellable")
function h(s, ctx)
    return nothing
end function
gi.connect(c, "cancelled", bound(h, { a: 1 }))' 'gi.connect cannot take a bound function'
    # CONTROL: a PLAIN function still connects, or the refusal would be
    # indistinguishable from breaking gi.connect.
    want 'gi.connect still takes a plain function' 'load gi
gi.require("Gio", "2.0")
c = gi.new("Gio.Cancellable")
function h(s)
    print "ran"
    return nothing
end function
gi.connect(c, "cancelled", h)
gi.call(c, "cancel")' 'ran'
else
    printf 'SKIP gi tiers (gi unavailable in this build)\n'
fi

echo "--- TIER 4: valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    vg_run ./gbasic tests/bound/bound_test.bas >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    if [ "$rc" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err"; echo "FAIL valgrind"; status=1
    else
        echo "PASS valgrind (no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
