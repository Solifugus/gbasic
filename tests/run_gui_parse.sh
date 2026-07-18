#!/usr/bin/env bash
# Parse-only headless smoke for the display examples (PLAN.md Phase D0.6, B6).
#
# examples/gui/ (GTK 3) and examples/gi/ (GObject-Introspection / GTK 4) can only
# be *run* on a live display with the right toolkit installed, so they are not in
# the golden suite. They can still ROT silently at the syntax level. This runner
# parses every one of them with `--ast` (parse, don't run: no eval, no display,
# no `load` resolution, no toolkit needed) and fails if any stops parsing.
#
# It intentionally does NOT execute them. The manual display procedure lives in
# examples/gui/README.md and examples/gi/README.md.
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
files=(examples/gui/*.bas examples/gi/*.bas)
shopt -u nullglob

if [ ${#files[@]} -eq 0 ]; then
    echo "FAIL run_gui_parse: no display examples found (expected examples/gui/*.bas, examples/gi/*.bas)"
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
