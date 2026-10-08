#!/usr/bin/env bash
# NAP-12 DataGrid — general virtualized data grid (stdlib/datagrid.bas) over the
# native GbRowModel adapter (src/modules/rowmodel.c, rowmodel.*).
#
# Three tiers, each skipping cleanly when its prerequisite is absent:
#
#   * HEADLESS MODEL (always, when the binary has the rowmodel adapter): the
#     native GListModel — count, deep get_item, item identity, change
#     notification, and the virtualization proof (N direct get_item calls touch
#     exactly N rows of a 1,000,000-row model). No GTK, no display.
#   * LIFETIME (with the GTK 4 typelib + a display): the factory/callback
#     ownership regression — grids built inside a helper that returns, with no
#     factory/column/view held anywhere outside the datagrid registry, must still
#     fire setup/bind and render correct cells under record/array churn, repeated
#     refresh (no duplicate handlers), multiple independent grids, and repeated
#     create/destroy.
#   * LOGIC (with the GTK 4 typelib + a display): builds real grids but never
#     shows them, asserting datagrid.cell displayed-value correctness for every
#     source shape, COW snapshot semantics, selection, and refresh —
#     deterministic, no rendering frame.
#   * DISPLAY smoke (with the GTK 4 typelib + a display): a real GtkColumnView
#     presented briefly under G_DEBUG=fatal-criticals — bounded realized rows for
#     a 1e6-row source, clean teardown, no GTK criticals.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."

export GBASIC_PATH="stdlib"

if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists girepository-2.0; then
    printf 'SKIP tests/datagrid (libgirepository-2.0 development files not available)\n'
    exit 0
fi
if ! pkg-config --exists gio-2.0; then
    printf 'SKIP tests/datagrid (gio-2.0 not available; rowmodel adapter not built)\n'
    exit 0
fi

make >/dev/null

stdout_file="$(mktemp)"
stderr_file="$(mktemp)"
trap 'rm -f "$stdout_file" "$stderr_file"' EXIT

# GTK renders scrollbars with a known-benign warning about the slider gizmo's
# reported minimum size; it is not a grid defect and never a critical. Strip it
# (and blank lines) before judging stderr so a real warning/critical still fails.
filter_benign() {
    grep -vE 'GtkGizmo|slider|min (width|height)|^$' "$1" || true
}

run_golden() {
    local source="$1"
    local expected="${source%.bas}.out"
    : >"$stdout_file"
    : >"$stderr_file"
    if timeout 90 ./gbasic "$source" >"$stdout_file" 2>"$stderr_file"; then
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
    if [ -n "$(filter_benign "$stderr_file")" ]; then
        printf 'FAIL %s (unexpected stderr — GTK warning/critical?)\n' "$source"
        filter_benign "$stderr_file"
        exit 1
    fi
}

# --- Headless model tier (always) ------------------------------------------
run_golden tests/datagrid/model_headless.bas

# --- Display tier: needs the GTK 4 typelib AND a display -------------------
if ! ./gbasic tests/datagrid/require_gtk.bas >"$stdout_file" 2>"$stderr_file"; then
    if grep -q 'gi.require: could not load namespace' "$stderr_file"; then
        printf 'SKIP tests/datagrid/{logic,display_smoke}.bas (GTK 4 typelib not available)\n'
        exit 0
    fi
    printf 'FAIL tests/datagrid (unexpected typelib probe error)\n'
    cat "$stderr_file"
    exit 1
fi

if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
    export G_DEBUG="${G_DEBUG:+$G_DEBUG,}fatal-criticals"
    run_golden tests/datagrid/logic.bas
    run_golden tests/datagrid/lifetime.bas
    run_golden tests/datagrid/display_smoke.bas
else
    printf 'SKIP tests/datagrid/{logic,lifetime,display_smoke}.bas (no display)\n'
fi

# --- Structural tier: the bound context replaced a per-bind SEARCH -----------
#
# GTK calls a bind handler with only (factory, item), so before 0.6.0 the handler
# recovered its column by SCANNING grid.columns for the factory that fired -- a
# linear search per bind, and a bind fires per visible cell per scroll. Each
# factory is connected with a bound context naming its grid and column ordinal now.
#
# READ FROM THE SOURCE, because the behavioural tiers above pass either way: a
# library that quietly kept its search would render identical cells.
if grep -q 'function _find_col' stdlib/datagrid.bas; then
    printf 'FAIL stdlib/datagrid.bas still defines _find_col; the per-bind search is not gone\n'
    exit 1
fi
printf 'PASS _find_col is gone (the per-bind factory search)\n'
if grep -q 'gi.connect(fac, "bind", bound(_bind' stdlib/datagrid.bas; then
    printf 'PASS the bind handler is connected with a bound context\n'
else
    printf 'FAIL the bind handler no longer carries a bound context\n'
    exit 1
fi
# THE CONTEXT MUST NOT HOLD THE COLUMN RECORD, which is the obvious thing and
# LEAKS: a column carries its `factory`, so a context holding one makes
# factory -> closure -> context -> factory, a reference cycle GObject cannot
# collect. The ordinal is what is carried instead, and this says why.
if grep -qE 'bound\(_bind, \{[^}]*col:' stdlib/datagrid.bas; then
    printf 'FAIL the bound context holds a column record, which cycles through its factory\n'
    exit 1
fi
printf 'PASS the context carries an ordinal, so no factory cycle\n'
# AND THE REGISTRY IS STILL THERE, asserted because it was EXPECTED TO GO and does
# not: it is also the id -> grid map for the whole API, and a gBASIC record is a
# value, so a record-valued handle could not carry mutation between calls. A library
# cannot hold mutable state of its own either (measured). If someone deletes it
# believing `bound` made it unnecessary, this says which half `bound` actually
# removed.
if grep -q '_DATAGRID' stdlib/datagrid.bas; then
    printf 'PASS the registry remains: it is the id->grid map, which bound() does not replace\n'
else
    printf 'FAIL the registry is gone -- if that is deliberate, the handle shape must have\n'
    printf '     changed too, since a record-valued handle cannot carry mutation\n'
    exit 1
fi
