#!/usr/bin/env bash
# Executable-docs gate (PLAN.md Phase D3, deliverable 4).
#
# The AI cookbook (docs/ai/COOKBOOK.md) promises that every idiom points at a
# real, suite-verified file. This gate enforces that promise: every file the
# cookbook references must (a) exist and (b) be wired into some test runner —
# either named in a tests/*.sh case list, or covered by a directory glob a runner
# uses (e.g. run_gui_parse.sh globs examples/gi/*.bas). It does not re-run the
# programs; the suites already do that.
set -u

cd "$(dirname "$0")/.."

COOK=docs/ai/COOKBOOK.md
if [ ! -f "$COOK" ]; then
    echo "FAIL run_docs_gate: $COOK not found"
    exit 1
fi

# Runners to search, excluding this gate itself.
runners=$(ls tests/*.sh tests/lsp/*.sh 2>/dev/null | grep -v 'run_docs_gate.sh')

refs=$(grep -oE '(examples|tests)/[A-Za-z0-9_./-]+\.(bas|gb)' "$COOK" | sort -u)
if [ -z "$refs" ]; then
    echo "FAIL run_docs_gate: no file references found in $COOK"
    exit 1
fi

status=0
for f in $refs; do
    if [ ! -f "$f" ]; then
        echo "FAIL missing        $f"
        status=1
        continue
    fi
    base=$(basename "$f")
    stem="${base%.*}"
    ext="${f##*.}"
    glob="$(dirname "$f")/*.$ext"
    if grep -qF "$stem" $runners 2>/dev/null || grep -qF "$glob" $runners 2>/dev/null; then
        echo "PASS wired          $f"
    else
        echo "FAIL not wired      $f"
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "run_docs_gate: some cookbook references are missing or unwired"
fi
exit "$status"
