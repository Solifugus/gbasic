#!/usr/bin/env bash
# NAP-7 SourceEditor / gtk.bas / gbasic.lang suite.
#
# Exercises the reusable gBASIC libraries that drive GtkSourceView 5 through the
# generic `gi` bridge (stdlib/gtk.bas, stdlib/sourceeditor.bas) plus the gBASIC
# syntax definition (stdlib/gtksourceview/gbasic.lang). Two tiers:
#
#   * HEADLESS (always, when the typelibs exist): the GtkSourceBuffer surface —
#     language discovery + assignment, text, cursor, marks, highlight. A
#     GtkSourceBuffer is not a widget, so no display is needed.
#   * DISPLAY smoke (only when a display is present): the GtkSourceView-dependent
#     surface — view creation, scrolling, and a GtkTextChildAnchor inline widget.
#
# Skips cleanly (never fails) when the prerequisites are absent:
#   1. libgirepository-2.0 dev files (HAVE_GIR=0)
#   2. the GTK 4 / GtkSource 5 typelibs
#   3. (display tier only) no X/Wayland display
#
# GBASIC_PATH=stdlib is set so `load gtk`/`load sourceeditor` resolve from the dev
# tree and the language manager finds stdlib/gtksourceview/gbasic.lang.
set -euo pipefail

cd "$(dirname "$0")/.."

export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"
export GBASIC_PATH="stdlib"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP tests/native_editor (libgirepository-2.0 development files not available)\n'
    exit 0
fi

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# --- Typelib gate: resolve GtkSource through gi.require ---------------------
if ! ./gbasic tests/native_platform/require_typelibs.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q 'gi.require: could not load namespace' "$stderr_file"; then
        printf 'SKIP tests/native_editor (GTK 4 / GtkSource 5 typelibs not available)\n'
        exit 0
    fi
    printf 'FAIL tests/native_editor (unexpected typelib probe error)\n'
    cat "$stderr_file"
    exit 1
fi

run_golden() {
    local name="$1"
    local source="tests/native_editor/$name.bas"
    local expected="tests/native_editor/$name.out"
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
}

# --- Headless tier (always) ------------------------------------------------
run_golden headless

# --- Display tier (only with a display) ------------------------------------
if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
    run_golden display_smoke
else
    printf 'SKIP tests/native_editor/display_smoke.bas (no display)\n'
fi
