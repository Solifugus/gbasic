#!/usr/bin/env bash
# NAP-8 Native Application Platform Spike suite (examples/native_workbench).
#
# The spike is one integrated gBASIC GTK4 application exercising the whole platform
# (NAP-0..7). It runs in modes so the non-display logic is verified headlessly and
# only the full UI needs a display:
#
#   inspect / process  — pure gBASIC (generic value inspector; process.run). No GI,
#                        no display: always run.
#   async              — the actor -> mailbox fd -> GLib-loop responsiveness proof
#                        (GLib main loop, no widgets). Needs libgirepository: gated.
#   smoke              — the full GTK4 UI built, auto-driven, and self-quitting with
#                        a deterministic transcript. Needs the GTK4/GtkSource
#                        typelibs and a display: gated + skipped cleanly otherwise.
#
# All display/GLib runs use G_DEBUG=fatal-criticals: any GLib critical aborts and
# fails the suite (benign Gtk-WARNING layout noise on stderr is ignored; only
# stdout is asserted byte-exact).
set -euo pipefail

cd "$(dirname "$0")/.."

export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"
export GBASIC_PATH="stdlib"

make >/dev/null

APP="examples/native_workbench/workbench.bas"
stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

run_mode() {
    local mode="$1"
    local expected="tests/native_workbench/$mode.out"
    : >"$stdout_file"
    : >"$stderr_file"
    if timeout 60 ./gbasic "$APP" "$mode" >"$stdout_file" 2>"$stderr_file"; then
        if diff -u "$expected" "$stdout_file"; then
            printf 'PASS %s [%s]\n' "$APP" "$mode"
        else
            printf 'FAIL %s [%s]\n' "$APP" "$mode"
            exit 1
        fi
    else
        status=$?
        printf 'FAIL %s [%s] (exit %d)\n' "$APP" "$mode" "$status"
        cat "$stderr_file"
        exit 1
    fi
}

# --- Pure-gBASIC tier (always) ---------------------------------------------
run_mode inspect
run_mode process

# --- GLib async tier (needs libgirepository) -------------------------------
if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists girepository-2.0; then
    run_mode async
else
    printf 'SKIP %s [async] (libgirepository-2.0 not available)\n' "$APP"
fi

# --- Display smoke tier (needs GTK4/GtkSource typelibs + a display) ---------
if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP %s [smoke] (libgirepository-2.0 not available)\n' "$APP"
    exit 0
fi
if ! ./gbasic tests/native_platform/require_typelibs.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q 'gi.require: could not load namespace' "$stderr_file"; then
        printf 'SKIP %s [smoke] (GTK 4 / GtkSource 5 typelibs not available)\n' "$APP"
        exit 0
    fi
    printf 'FAIL %s [smoke] (unexpected typelib probe error)\n' "$APP"
    cat "$stderr_file"
    exit 1
fi
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    printf 'SKIP %s [smoke] (no display)\n' "$APP"
    exit 0
fi
run_mode smoke
