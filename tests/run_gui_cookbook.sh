#!/usr/bin/env bash
# The GUI cookbook (docs/gui_cookbook.md) -- a tutorial that cannot lie.
#
# Same four tiers as the xlsx/chart/datetime cookbooks -- RUN, CODE, OUTPUT,
# COVER -- so the page owns neither the code nor the output and cannot drift
# from either. See tests/run_chart_cookbook.sh for what each tier proves.
#
# WHAT MAKES A GUI COOKBOOK CHECKABLE AT ALL: `gtk.init()` needs a display, but
# SHOWING a window does not. Every recipe builds real widgets and interrogates
# them, so the suite asserts on genuine GTK objects with nothing on screen --
# the same technique tests/run_gtkui.sh and tests/run_datagrid.sh use.
#
# Run under G_DEBUG=fatal-criticals, so a GTK critical -- the class of warning
# that means "you have used this API wrongly" -- aborts the recipe instead of
# scrolling past into a golden.
#
# SKIPS ENTIRELY, and says so, without a display or the GTK 4 typelib. That is
# honest rather than convenient: these recipes cannot run headless, and a suite
# that silently passed on a build server while testing nothing would be worse
# than one that admits it.
set -u

cd "$(dirname "$0")/.."

DOC=docs/gui_cookbook.md
DIR=examples/gui_cookbook

status=0
pass=0
skip=0

note_fail() { printf 'FAIL %s\n' "$1"; status=1; }

if [ ! -f "$DOC" ]; then
    note_fail "run_gui_cookbook: $DOC not found"
    exit 1
fi

if [ ! -x ./gbasic ]; then
    make >/dev/null 2>&1 || { note_fail "run_gui_cookbook: build failed"; exit 1; }
fi

# A display AND the GTK 4 typelib. Missing either is a skip, not a failure.
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    printf 'SKIP run_gui_cookbook (no display)\n'
    exit 0
fi
if ! ls /usr/lib/*/girepository-*/Gtk-4.0.typelib >/dev/null 2>&1; then
    printf 'SKIP run_gui_cookbook (no GTK 4 typelib)\n'
    exit 0
fi
export G_DEBUG=fatal-criticals

# Spelled literally rather than through $DIR so run_docs_gate.sh can see that
# this directory glob is covered by a runner -- the gate greps for the literal
# `examples/gui_cookbook/*.bas`, and a variable would read as unwired.
recipes=$(ls examples/gui_cookbook/*.bas 2>/dev/null | sed 's|.*/||; s|\.bas$||' | sort)
if [ -z "$recipes" ]; then
    note_fail "run_gui_cookbook: no recipes in $DIR"
    exit 1
fi

# Extract the fenced block that follows a given marker in the doc.
extract_block() {
    awk -v marker="$1" '
        $0 == marker { found = 1; next }
        found && /^```/ { if (infence) { exit } ; infence = 1; next }
        found && infence { print }
    ' "$DOC"
}

# --- RUN + OUTPUT + CODE ------------------------------------------------------
for r in $recipes; do
    bas="$DIR/$r.bas"
    out="$DIR/$r.out"

    if [ ! -f "$out" ]; then
        note_fail "$bas (no committed .out)"
        continue
    fi

    actual="$(mktemp)"
    errf="$(mktemp)"
    # GBASIC_PATH so the recipes that `load grid`/`consolidate`/`dbframe` resolve.
    GBASIC_PATH=stdlib ./gbasic "$bas" >"$actual" 2>"$errf"
    rc=$?

    if grep -q 'not available in this build' "$errf"; then
        printf 'SKIP %s (module compiled out)\n' "$bas"
        skip=$((skip + 1))
        rm -f "$actual" "$errf"
        continue
    fi

    if [ "$rc" != "0" ]; then
        note_fail "$bas (exit $rc)"
        head -5 "$errf"
        rm -f "$actual" "$errf"
        continue
    fi

    if ! diff -u "$out" "$actual" >/dev/null 2>&1; then
        note_fail "$bas (stdout does not match $out)"
        diff -u "$out" "$actual" | head -20
        rm -f "$actual" "$errf"
        continue
    fi
    printf 'PASS run %s\n' "$bas"
    pass=$((pass + 1))
    rm -f "$actual" "$errf"

    # The page's code block must BE the file.
    blk="$(mktemp)"
    extract_block "<!--CODE:$r-->" > "$blk"
    if [ ! -s "$blk" ]; then
        note_fail "$DOC (no code block for $r -- add <!--CODE:$r--> and run tools/sync_gui_cookbook.sh)"
    elif ! diff -u "$bas" "$blk" >/dev/null 2>&1; then
        note_fail "$DOC code block for $r differs from $bas -- run tools/sync_gui_cookbook.sh"
        diff -u "$bas" "$blk" | head -15
    else
        printf 'PASS code %s\n' "$r"
        pass=$((pass + 1))
    fi
    rm -f "$blk"

    # The page's output block must BE the golden.
    blk="$(mktemp)"
    extract_block "<!--OUT:$r-->" > "$blk"
    if [ ! -s "$blk" ]; then
        note_fail "$DOC (no output block for $r)"
    elif ! diff -u "$out" "$blk" >/dev/null 2>&1; then
        note_fail "$DOC output block for $r differs from $out -- run tools/sync_gui_cookbook.sh"
        diff -u "$out" "$blk" | head -15
    else
        printf 'PASS out  %s\n' "$r"
        pass=$((pass + 1))
    fi
    rm -f "$blk"
done

# --- COVER: no recipe undocumented, no marker without a file ------------------
for r in $recipes; do
    if ! grep -qF "<!--CODE:$r-->" "$DOC"; then
        note_fail "$DOC does not document recipe $r"
    fi
done
for m in $(grep -oE '<!--CODE:[A-Za-z0-9_]+-->' "$DOC" | sed 's/<!--CODE://; s/-->//'); do
    if [ ! -f "$DIR/$m.bas" ]; then
        note_fail "$DOC references recipe $m, which has no file in $DIR"
    fi
done

printf '\nrun_gui_cookbook: PASS=%d SKIP=%d\n' "$pass" "$skip"
exit "$status"
