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
COOKBOOK=gui
RECIPE_GLOB='examples/gui_cookbook/*.bas'

cookbook_precheck() {
    if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
        printf 'SKIP run_gui_cookbook (no display)\n'
        exit 0
    fi
    if ! ls /usr/lib/*/girepository-*/Gtk-4.0.typelib >/dev/null 2>&1; then
        printf 'SKIP run_gui_cookbook (no GTK 4 typelib)\n'
        exit 0
    fi
    export G_DEBUG=fatal-criticals
}

. "$(dirname "$0")/cookbook_harness.sh"
