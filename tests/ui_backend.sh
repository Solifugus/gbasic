# shellcheck shell=bash
# WHICH GUI BACKEND A SUITE IS MEASURING (docs/ui_design.md §8 step 6).
#
# Source this from a GUI suite, after tests/build_has.sh. A tier is one of
# three classes, and its TIER line says which, as `TIER [N] name: ...`:
#
#   [N]  needs no backend: grammar, declarations, ui.check, source tripwires.
#   [C]  the contract: a gBASIC program drives verbs and what is asserted is
#        what gBASIC observes. Owed by every backend that has the groups the
#        tier needs; it asks with `ui_tier_needs <group>` first.
#   [Q]  Qt's own: a C++ probe linking qt_backend, pixels of Qt's drawing.
#        It asks `ui_tier_qt` first and is never run under another backend.
#   [T]  the terminal backend's own (docs/tui_design.md): its screen as text,
#        a real pty, the selection rule. It asks `ui_tier_tui` first.
#
# tests/run_ui_vocabulary.sh `classes` refuses a GUI suite's TIER line with
# no class, so the sort cannot rot as tiers are added.
#
#   ui_backends              the backends this ./gbasic was built with
#   ui_each_backend "$0" "$@"  at the top of a suite: run the suite once per
#                            backend with GBASIC_UI set, then once more with
#                            every group withheld, and exit with the worst
#                            status. With no backend, one pass.
#   ui_tier_needs GROUP...   0 if the backend under test has every group;
#                            else prints the SKIP naming them and answers 1
#   ui_tier_qt               0 if the backend under test is Qt; else SKIP, 1

# THE BUILD'S BACKENDS, asked of the binary: the sentence GBASIC_UI naming no
# backend is refused with ends "it has: a, b" (run_ui_vocabulary `seam` pins
# it). A build with none says so differently and lists nothing.
#
# A BACKEND STILL BEING BUILT IS LEFT OUT of every suite's passes, named in
# UI_SUITES_SKIP_BACKENDS (default `tui`): increment 3 of docs/tui_design.md
# draws the layout and the first leaves, and every [C] tier using a table, a
# menu or a dialog would fail on it for a reason this harness already knows.
# tests/run_ui_tui.sh measures it instead. When it covers the core (§7,
# increment 5) the default empties, and each suite owes it its [C] tiers;
# UI_SUITES_SKIP_BACKENDS= runs them on it now, to see how far it is.
#
# A SUITE THAT PASSES ON IT SAYS SO, `UI_SUITE_ON_TUI=1` before
# `ui_each_backend`, and then the gate runs it there too (increment 5: the
# dialog, chooser and modal suites, and those increment 4 measured passing).
# The default empties when the editor (increment 6) brings the rest.
ui_backends() {
    local skip="${UI_SUITES_SKIP_BACKENDS-tui}"
    if [ -z "${UI_SUITES_SKIP_BACKENDS+set}" ] && [ "${UI_SUITE_ON_TUI:-0}" = 1 ]; then
        skip=""
    fi
    ui_backends_built | while read -r be; do
        case " $skip " in
            *" $be "*) ;;
            *) printf '%s\n' "$be" ;;
        esac
    done
}

ui_backends_built() {
    local probe out
    probe="$(mktemp)"
    printf 'program main()\n    ui.run()\nend program\n' > "$probe"
    out=$(GBASIC_UI="(listing)" GBASIC_UI_WITHHOLD= ./gbasic "$probe" 2>&1)
    rm -f "$probe"
    printf '%s\n' "$out" | sed -n 's/.*this build has no such backend -- it has: //p' \
        | tr ',' ' ' | tr -s ' ' '\n' | sed '/^$/d'
}

# The backend this pass measures: GBASIC_UI, or the build's first.
ui_backend_now() {
    if [ -n "${GBASIC_UI:-}" ]; then
        printf '%s\n' "$GBASIC_UI"
    else
        ui_backends | head -1
    fi
}

ui_each_backend() {
    local suite="$1"
    shift
    [ -f "$suite" ] || suite="tests/$(basename "$suite")"   # run from elsewhere
    if [ -n "${UI_SUITE_PASS:-}" ]; then
        return 0   # already inside one pass
    fi
    local list be rc worst=0
    list=$(ui_backends)
    if [ -z "$list" ]; then
        export UI_SUITE_PASS=1   # no backend: this run is the only pass
        return 0
    fi
    for be in $list; do
        printf '== backend %s\n' "$be"
        # the terminal backend draws with no terminal at this size, as Qt
        # draws offscreen: a suite's tiers run headless
        if [ "$be" = tui ]; then
            export GBASIC_TUI_SCREEN="${GBASIC_TUI_SCREEN:-120x40}"
        fi
        UI_SUITE_PASS=1 GBASIC_UI="$be" bash "$suite" "$@"
        rc=$?
        [ "$rc" -gt "$worst" ] && worst=$rc
    done
    # THE SMALLEST BACKEND: the first one again with EVERY group withheld,
    # unless the caller withheld some already. Every tier using a group must
    # say so (ui_tier_needs) and skip here, or this pass fails -- which is
    # what keeps the gating true as tiers are added, before a backend that
    # really lacks a group exists to find out.
    if [ -z "${GBASIC_UI_WITHHOLD:-}" ]; then
        be=$(printf '%s\n' "$list" | head -1)
        printf '== backend %s, every capability group withheld\n' "$be"
        UI_SUITE_PASS=1 UI_WITHHELD_PASS=1 GBASIC_UI="$be" \
            GBASIC_UI_WITHHOLD="highlighting docking stylesheet figures file_dialogs notebook icons windows" \
            bash "$suite" "$@"
        rc=$?
        [ "$rc" -gt "$worst" ] && worst=$rc
    fi
    exit "$worst"
}

ui_tier_needs() {
    local g missing="" probe out
    probe="$(mktemp)"
    for g in "$@"; do
        printf 'program main()\n    print ui.can("%s")\nend program\n' "$g" > "$probe"
        out=$(./gbasic "$probe" 2>/dev/null)
        [ "$out" = "true" ] || missing="$missing $g"
    done
    rm -f "$probe"
    if [ -n "$missing" ]; then
        if [ -z "$(ui_backends)" ]; then
            printf '  SKIP (this build has no GUI backend -- this tier was NOT measured)\n'
        else
            printf '  SKIP (the %s backend has not:%s -- this tier was NOT measured)\n' \
                "$(ui_backend_now)" "$missing"
        fi
        return 1
    fi
    return 0
}

# 0 if this build has the tui backend (built even when no suite pass runs
# on it: UI_SUITES_SKIP_BACKENDS); else SKIP, 1
ui_tier_tui() {
    if ! ui_backends_built | grep -qx tui; then
        printf '  SKIP (this build has no tui backend -- NOT measured)\n'
        return 1
    fi
    return 0
}

ui_tier_qt() {
    local be
    be=$(ui_backend_now)
    if [ "$be" != "qt" ]; then
        printf '  SKIP (Qt'"'"'s own tier, and the backend is %s -- NOT measured)\n' "${be:-none}"
        return 1
    fi
    return 0
}

# A PROGRAM REFUSED FOR A GROUP the backend under test lacks: prints the
# group and answers 0, for a runner of whole programs (the examples, the
# cookbook) to skip that one by name. Sound because a backend with the group
# cannot produce the sentence, and the sentence itself is proved in
# run_ui_vocabulary `groups`.
ui_group_refused() {   # $1 = a file holding the program's output
    sed -n 's/.* needs `\([a-z_]*\)`, which the [a-z_]* backend does not have -- ask ui\.can(.*/\1/p' "$1" | head -1 | grep .
}

# A TIER THAT MEASURES NOTHING NEW WITHOUT ITS GROUPS: valgrind over the same
# fixture the backend's own pass just ran it on. Skipped in the
# every-group-withheld pass, which exists to find tiers using a group they do
# not declare; rerunning valgrind there cost about a third more GUI-suite CPU
# (7m41s against 5m42s, 2026-11-16) for nothing new.
ui_tier_once() {
    if [ -n "${UI_WITHHELD_PASS:-}" ]; then
        printf '  SKIP (measured in the backend'"'"'s own pass; this pass withholds every group)\n'
        return 1
    fi
    return 0
}
