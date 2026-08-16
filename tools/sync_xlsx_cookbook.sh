#!/usr/bin/env bash
# Expand the code/output markers in docs/xlsx_cookbook.md from the real files.
#
# The cookbook shows each recipe's source and its output inline, which is what
# makes it readable -- and which is exactly how a tutorial rots, because the
# prose keeps saying what the code used to do. So the page does not OWN either
# block: `examples/xlsx_cookbook/NN_name.bas` owns the code and `.out` owns the
# output, this script copies them in, and tests/run_xlsx_cookbook.sh fails while
# the page disagrees with them.
#
# Run after changing a recipe:  tools/sync_xlsx_cookbook.sh
# It is idempotent -- it replaces the fenced block following each marker.
set -euo pipefail

cd "$(dirname "$0")/.."

DOC=docs/xlsx_cookbook.md
DIR=examples/xlsx_cookbook

[ -f "$DOC" ] || { echo "sync_xlsx_cookbook: $DOC not found" >&2; exit 1; }

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

# Walk the doc. On a marker, emit the marker then the file's fenced block, and
# swallow any fenced block that already follows it.
awk -v dir="$DIR" '
function emit_file(path, fence,    line, n) {
    print "```" fence
    n = 0
    while ((getline line < path) > 0) { print line; n++ }
    close(path)
    if (n == 0) { print "(no output)" }
    print "```"
    print ""
}
# state: 0 normal, 1 skipping blanks before an old fence, 2 inside it,
# 3 skipping blanks after it. Written as a state machine so re-running is a
# no-op -- a sync tool that is not idempotent is one nobody runs twice.
/^<!--CODE:[A-Za-z0-9_]+-->$/ {
    name = $0; sub(/^<!--CODE:/, "", name); sub(/-->$/, "", name)
    print $0; print ""
    emit_file(dir "/" name ".bas", "basic")
    state = 1; next
}
/^<!--OUT:[A-Za-z0-9_]+-->$/ {
    name = $0; sub(/^<!--OUT:/, "", name); sub(/-->$/, "", name)
    print $0; print ""
    emit_file(dir "/" name ".out", "")
    state = 1; next
}
{
    if (state == 1) {
        if ($0 == "") { next }
        if ($0 ~ /^```/) { state = 2; next }
        state = 0
    } else if (state == 2) {
        if ($0 ~ /^```/) { state = 3 }
        next
    } else if (state == 3) {
        if ($0 == "") { next }
        state = 0
    }
    print
}
' "$DOC" > "$tmp"

if cmp -s "$tmp" "$DOC"; then
    echo "sync_xlsx_cookbook: already in step"
else
    cp "$tmp" "$DOC"
    echo "sync_xlsx_cookbook: updated $DOC"
fi
