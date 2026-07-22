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
set -euo pipefail

cd "$(dirname "$0")/.."

export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"
export GBASIC_PATH="stdlib"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP tests/gtkui (libgirepository-2.0 development files not available)\n'
    exit 0
fi

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

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

# --- Headless tier (always) ------------------------------------------------
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
