#!/usr/bin/env bash
# GATE-SCOPE: whole-tree -- parses every program in examples/qt by GLOB, so a new or edited one is in scope without being named
# Parse-only smoke for the display examples in examples/qt.
#
# run_qt_examples.sh RUNS these, but it skips on a build without Qt, and they
# can still rot at the syntax level there. This runner parses every one with
# `--ast` (parse, don't run: no eval, no display, no toolkit needed) and fails
# if any stops parsing. The GTK and gi example directories it once covered were
# removed with GTK and gi on 2026-10-22.
set -u

cd "$(dirname "$0")/.."

if [ ! -x ./gbasic ]; then
    if ! make gbasic >/dev/null 2>&1; then
        echo "FAIL run_gui_parse: could not build gbasic"
        exit 1
    fi
fi

status=0
shopt -s nullglob
files=(examples/qt/*.bas)
shopt -u nullglob

if [ ${#files[@]} -eq 0 ]; then
    echo "FAIL run_gui_parse: no display examples found (expected examples/qt/*.bas)"
    exit 1
fi

for f in "${files[@]}"; do
    err="$(./gbasic --ast "$f" 2>&1 >/dev/null)"
    if [ $? -eq 0 ]; then
        printf 'PASS parse %s\n' "$f"
    else
        printf 'FAIL parse %s\n' "$f"
        [ -n "$err" ] && printf '%s\n' "$err"
        status=1
    fi
done

exit "$status"
