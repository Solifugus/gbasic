#!/usr/bin/env bash
# NAP-11 gtkui — dynamic declarative widget-tree reconciler (stdlib/gtkui.bas).
#
# The reconciler is pure gBASIC over the generic `gi` bridge (GTK 4 is NOT
# linked). Two tiers:
#
#   * HEADLESS (always, when the `gi` module is built): the diff/classification
#     logic — container-kind table, all-keyed matcher, reuse-vs-replace test,
#     record helpers. No widgets, no GTK init, no display.
#   * DISPLAY smoke (only with a display + the GTK 4 typelib): the real
#     reconciler — mount, prop update with instance reuse, keyed insert/remove/
#     reorder, type replacement, a 3-level nested tree, the native-widget escape
#     hatch, single-fire signals after repeated reconcile, and unmount. Run under
#     G_DEBUG=fatal-criticals so any GTK warning/critical fails the suite.
#
# Skips cleanly (never fails) when prerequisites are absent:
#   1. libgirepository-2.0 dev files (HAVE_GIR=0)  -> skip all
#   2. the GTK 4 typelib                            -> skip the display tier
#   3. no X/Wayland display                         -> skip the display tier
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."

export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"
export GBASIC_PATH="stdlib"

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

. "$(dirname "$0")/build_has.sh"
# ASK THE BINARY, NOT THE MACHINE. This asked `pkg-config --exists
# girepository-2.0`, which answers whether the DEV FILES are installed -- a
# different question from whether THIS gbasic was built with `gi`, and the two
# part company exactly where it matters. Measured 2026-10-14 on
# `make GIR_AVAILABLE=0` with the dev files present: pkg-config said yes, the
# tiers ran, and they FAILED with "gobject-introspection support is
# unavailable" -- a red gate describing a correct build. That is the trap
# run_ldap.sh and run_web_tls.sh each had to fix, and build_has.sh exists
# because of it; its comment even records `gi`'s own wording.
have_gir=1
build_has gi || have_gir=0

run_selfchecking() {   # run <name> <min-ok>: a fixture that states its own answers
    local name="$1" floor="$2" out bad ok
    out="$({ timeout 60 ./gbasic "tests/gtkui/$name.bas" 2>&1 </dev/null || true; })"
    bad="$(printf '%s\n' "$out" | grep -c '^MISMATCH' || true)"
    ok="$(printf '%s\n' "$out" | grep -c '^ok  ' || true)"
    if [ "${bad:-0}" -ne 0 ]; then
        printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
        printf 'FAIL tests/gtkui/%s.bas\n' "$name"; exit 1
    fi
    if [ "${ok:-0}" -lt "$floor" ]; then
        printf 'FAIL tests/gtkui/%s.bas (only %s checks ran; at least %s expected)\n' \
            "$name" "$ok" "$floor"
        printf '%s\n' "$out" | tail -3 | sed 's/^/  /'
        exit 1
    fi
    printf 'PASS tests/gtkui/%s.bas (%s checks)\n' "$name" "$ok"
}

run_golden() {
    local name="$1"
    local source="tests/gtkui/$name.bas"
    local expected="tests/gtkui/$name.out"
    : >"$stdout_file"
    : >"$stderr_file"
    if timeout 60 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
        if diff -u "$expected" "$stdout_file"; then
            printf 'PASS %s\n' "$source"
        else
            printf 'FAIL %s\n' "$source"
            exit 1
        fi
    else
        status=$?
        printf 'FAIL %s (exit %d)\n' "$source" "$status"
        cat "$stderr_file"
        exit 1
    fi
    if [ -s "$stderr_file" ]; then
        printf 'FAIL %s (unexpected stderr — GTK warning/critical?)\n' "$source"
        cat "$stderr_file"
        exit 1
    fi
}

# --- SEAM tier: THE RECONCILER WITH NO GTK AT ALL, and it runs FIRST ---------
#
# A SEAM WITH ONE IMPLEMENTATION IS INDISTINGUISHABLE FROM NO SEAM. The
# reconciler could call `b.create` instead of `gi.new` and still be welded to GTK
# in a dozen hidden ways, and both tiers below would stay green. So this one
# drives `gtkui` end to end over a RECORDING backend defined in the fixture
# itself: no `load gi`, no typelib, no display, no widget.
#
# IT NEEDS NO GIR GATE, AND THAT IS A GATE SHRINK REPAIRED RATHER THAN A
# CONVENIENCE. Until 2026-10-14 this whole suite exited 0 with one SKIP line when
# the libgirepository DEVELOPMENT files were absent -- including the tier that
# needs no widgets -- because `stdlib/gtkui.bas` opened with `load gi` and `load`
# is a declaration that cannot be conditional. The reconciler has no `load` now,
# so on a machine with no GObject-Introspection at all this tier still runs and
# the suite still asserts something. "A gate you can turn off by not installing
# something is a gate that shrinks" is this tree's own rule.
run_selfchecking seam 20

if [ "$have_gir" -eq 0 ]; then
    printf 'SKIP tests/gtkui headless+display (no `gi` in THIS build) -- the SEAM\n'
    printf '     tier above still ran its 23 checks, which is the half that needs no\n'
    printf '     GTK. Before 2026-10-14 the whole suite went quiet here.\n'
    exit 0
fi

# --- Headless tier (needs the gi module built) ------------------------------
run_golden headless

# --- Display tier: needs the GTK 4 typelib AND a display -------------------
if ! ./gbasic tests/gtkui/require_gtk.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q 'gi.require: could not load namespace' "$stderr_file"; then
        printf 'SKIP tests/gtkui/smoke.bas (GTK 4 typelib not available)\n'
        exit 0
    fi
    printf 'FAIL tests/gtkui (unexpected typelib probe error)\n'
    cat "$stderr_file"
    exit 1
fi

if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
    run_golden smoke
else
    printf 'SKIP tests/gtkui/smoke.bas (no display)\n'
fi
