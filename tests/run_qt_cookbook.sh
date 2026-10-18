#!/usr/bin/env bash
# The Qt cookbook (docs/qt_cookbook.md) -- a tutorial that cannot lie.
# RUN / CODE / OUTPUT / COVER, all in tests/cookbook_harness.sh.
#
# WHAT MAKES A GUI COOKBOOK CHECKABLE AT ALL is that `QT_QPA_PLATFORM=offscreen`
# is not a test-only arrangement -- it is how a gBASIC GUI program renders on a
# headless server. Every recipe builds real Qt widgets and interrogates them,
# so the page's output is a golden over genuine objects with nothing on screen,
# the same technique run_qt_backend.sh and run_gui_cookbook.sh use.
#
# A RECIPE DOES NOT CALL `qt.run()`, and the page says so rather than leaving
# it to be noticed: `qt.run()` hands Qt the thread until something calls
# `qt.stop()`, so a recipe that reached it would HANG -- measured, killed at 60s
# under the offscreen platform. Each one opens its window, fires what a person
# would have clicked with `qt.invoke`, prints what it can observe and stops,
# which is also the `check` idiom every program in examples/qt/ carries.
#
# THE SYNTAX PATH IS SET for recipe 09, which highlights a `source` widget:
# the engine wants `<dir>/syntax/*.xml` and `make` derives that layout from the
# one definition this repo ships. Without it the recipe would report "none
# installed" and its golden would be a fact about the machine -- which is
# precisely the trap §0.14 records, where a copy hand-installed in a developer's
# home made the whole thing appear to work.
. "$(dirname "$0")/portable.sh"
export QT_QPA_PLATFORM=offscreen
export GBASIC_SYNTAX_PATH=editors/kate

# A BUILD WITHOUT KSyntaxHighlighting still renders a `source` widget and still
# edits text -- it just reports no definition, so recipe 09's output is
# different and correct. Skipped by ASKING THE BINARY rather than by probing
# the machine, which is tests/build_has.sh's own argument.
cookbook_recipe_skip() {
    case "$1" in
        09_code_editor)
            if [ "$(printf 'print qt.can_highlight()\n' > /tmp/gb_hl_$$.bas
                    ./gbasic /tmp/gb_hl_$$.bas 2>/dev/null; rm -f /tmp/gb_hl_$$.bas)" = "false" ]; then
                printf 'SKIP examples/qt_cookbook/09_code_editor.bas (no KSyntaxHighlighting in this build)\n'
                return 0
            fi
            return 1 ;;
    esac
    return 1
}

COOKBOOK=qt
RECIPE_GLOB='examples/qt_cookbook/*.bas'

. "$(dirname "$0")/cookbook_harness.sh"
