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

# ---- THE GUI PLAYER, which is the same world with its control flow turned
# inside out. The text game is a read-eval-print loop; a GUI cannot have one,
# because Qt owns the loop. So the GUI player is a proof rather than a demo --
# if a whole small application can be built on that surface, the surface works.
#
# IT PLAYS ITSELF, through the same handlers a mouse would fire: take the key,
# walk north, find the gate shut, open it, walk through. The ASSERTIONS ARE THE
# OUTCOME of each of those, because "it ran" is satisfied by a window that
# opened and did nothing.
#
# SKIPS WITH ITS REASON where Qt is absent, and says that the GUI player was
# NOT exercised -- a green line that ran nothing is the other way a gate goes
# quiet.
GUI_OUT="${TMPDIR:-/tmp}/gbasic_bag_gui_$$.out"
set +e
QT_QPA_PLATFORM=offscreen timeout -k 5 60 "$ROOT/gbasic" --line-buffered \
    "$ROOT/examples/bag/gui.bas" check > "$GUI_OUT" 2>&1
GUI_RC=$?
set -e
if grep -q "Qt support is not available" "$GUI_OUT"; then
    printf 'SKIP the GUI player was NOT exercised (no Qt in this build)\n'
elif grep -q "No display is available" "$GUI_OUT"; then
    printf 'SKIP the GUI player was NOT exercised (no display)\n'
elif [ "$GUI_RC" != "0" ]; then
    tail -5 "$GUI_OUT"
    printf 'FAIL the GUI player exited %s\n' "$GUI_RC"
    rm -f "$GUI_OUT"
    exit 1
else
    # THE WHOLE GAME, in the order it has to happen. The gate is the one that
    # matters: North is DISABLED in the Garden until the key is used, and
    # enabled afterwards, which is the world changing a widget.
    grep -Fq "start:        The Cellar" "$GUI_OUT"
    grep -Fq "north open:   true" "$GUI_OUT"
    grep -Fq "east open:    false" "$GUI_OUT"
    grep -Fq "after taking: You are carrying: brass key" "$GUI_OUT"
    grep -Fq "moved to:     The Garden" "$GUI_OUT"
    grep -Fq "gate shut:    false" "$GUI_OUT"
    grep -Fq "gate open:    true" "$GUI_OUT"
    grep -Fq "and through:  The Tunnel" "$GUI_OUT"
    grep -Fq "after drop:   You are carrying nothing." "$GUI_OUT"
    # AND THE TABLE FOLLOWED THE ROOM, which needed `qt.refresh` and which the
    # first draft of the example could not do: the key is in the Tunnel now.
    grep -Fq "here now:     1 thing(s)" "$GUI_OUT"
    grep -Fq "and it is:    brass key" "$GUI_OUT"
    printf 'the GUI player played itself through: cellar -> key -> garden -> gate -> tunnel\n'
fi
rm -f "$GUI_OUT"
