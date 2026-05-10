#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="${TMPDIR:-/tmp}/gbasic_bag_smoke_$$.bag"
OUTPUT="${TMPDIR:-/tmp}/gbasic_bag_smoke_$$.out"
GENERATED="$ROOT/examples/bag/generated_adventure.bas"
BACKUP="$(mktemp "${TMPDIR:-/tmp}/gbasic_bag_generated_XXXXXX")"
HAD_GENERATED=0

if [ -f "$GENERATED" ]; then
    cp "$GENERATED" "$BACKUP"
    HAD_GENERATED=1
fi

cleanup() {
    if [ "$HAD_GENERATED" -eq 1 ]; then
        cp "$BACKUP" "$GENERATED"
    else
        rm -f "$GENERATED"
    fi
    rm -f "$PROJECT" "$OUTPUT" "$BACKUP"
}
trap cleanup EXIT

printf 'n\n1\nSmoke Room\nA small room.\n0\n0\n0\n0\n0\n0\n9\n%s\n10\n%s\n2\n7\n8\n' "$PROJECT" "$PROJECT" |
    "$ROOT/gbasic" "$ROOT/examples/bag/bag.bas" > "$OUTPUT"

grep -Fq "Saved project to $PROJECT" "$OUTPUT"
grep -Fq "Loaded project from $PROJECT" "$OUTPUT"
grep -Fq "[1] Smoke Room" "$OUTPUT"
grep -Fq "Generated examples/bag/generated_adventure.bas" "$OUTPUT"
test -s "$PROJECT"
