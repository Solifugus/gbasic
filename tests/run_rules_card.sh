#!/usr/bin/env bash
# docs/ai/RULES.md, the rules card (2026-11-26, the fourth AI aid): fifteen
# traps a model writing gBASIC meets first, one line and one example each.
# The card is executable, so it cannot drift from the binary.
#
# Tiers:
#   blocks   every ```basic block opens with an expectation. `' wrong -> TEXT`:
#            the program must fail (exit nonzero) and its first stderr line
#            hold TEXT. `' right -> OUT`: it must exit 0, write nothing to
#            stderr, and print exactly OUT (`|` separating lines).
#   shape    15 numbered rules, each with a right block; a block with no
#            expectation fails (a control, planted, must be caught).
#   binary   `gbasic --rules` prints the card byte for byte as this tree has it.
# Headless, no network, never skips.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL run_rules_card: build failed"; exit 1; }

status=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
CARD=docs/ai/RULES.md
G="$(pwd)/gbasic"

# split a card into $dir/NN.bas, one per ```basic block; prints the count
split_card() {   # split_card <card> <dir>
    mkdir -p "$2"
    awk -v dir="$2" '
        /^```basic[ \t]*$/ { n++; f = sprintf("%s/%02d.bas", dir, n); inb = 1; next }
        /^```[ \t]*$/ && inb { inb = 0; close(f); next }
        inb { print > f }
        END { print n + 0 }
    ' "$1"
}

# check every block in a directory; prints one line per failure
check_blocks() {   # check_blocks <dir>
    local f head kind want out err rc
    for f in "$1"/*.bas; do
        head=$(head -1 "$f")
        case "$head" in
            "' wrong -> "*) kind=wrong; want=${head#"' wrong -> "} ;;
            "' right -> "*) kind=right; want=${head#"' right -> "} ;;
            *) echo "$(basename "$f"): no expectation on its first line: $head"; continue ;;
        esac
        out=$(cd "$work" && env -u GBASIC_RULES_CARD_UNSET GBASIC_PATH="$OLDPWD/stdlib" "$G" "$f" 2>"$work/err")
        rc=$?
        err=$(cat "$work/err")
        if [ "$kind" = wrong ]; then
            if [ "$rc" = 0 ] && [ -z "$err" ]; then
                echo "$(basename "$f"): wrong block ran clean: $out"
            else
                case "$(printf '%s\n' "$err" | head -1)" in
                    *"$want"*) ;;
                    *) echo "$(basename "$f"): wanted: $want  got: $(printf '%s\n' "$err" | head -1)" ;;
                esac
            fi
        else
            local expected
            expected=$(printf '%s' "$want" | tr '|' '\n')
            if [ "$rc" != 0 ] || [ -n "$err" ] || [ "$out" != "$expected" ]; then
                echo "$(basename "$f"): right block: exit $rc, printed [$out], wanted [$expected], stderr [$err]"
            fi
        fi
    done
}

echo "--- TIER blocks ---"
n=$(split_card "$CARD" "$work/card")
problems=$(check_blocks "$work/card")
if [ -z "$problems" ] && [ "$n" -ge 25 ]; then
    pass "$n blocks: every wrong one fails with its message, every right one prints its output"
else
    fail "card blocks ($n):"
    printf '%s\n' "$problems"
fi

echo "--- TIER shape ---"
rules=$(grep -cE '^\*\*[0-9]+\. ' "$CARD")
rights=$(grep -c "^' right -> " "$CARD")
if [ "$rules" = 15 ] && [ "$rights" -ge "$rules" ]; then
    pass "15 numbered rules, $rights right blocks"
else
    fail "shape: $rules rules, $rights right blocks"
fi
# the controls: a block with no expectation, a wrong block that runs clean, and
# a right block printing something else must each be caught
printf '```basic\nprint 1\n```\n```basic\n'"' wrong -> anything"'\nprint 1\n```\n```basic\n'"' right -> 2"'\nprint 1\n```\n' >"$work/planted.md"
split_card "$work/planted.md" "$work/planted" >/dev/null
caught=$(check_blocks "$work/planted" | wc -l | tr -d ' ')
if [ "$caught" = 3 ]; then
    pass "three planted bad blocks are each caught"
else
    fail "planted controls: $caught of 3 caught"
fi

echo "--- TIER binary ---"
if "$G" --rules | cmp -s - "$CARD"; then
    pass "gbasic --rules prints the card byte for byte"
else
    fail "gbasic --rules differs from $CARD"
fi

[ "$status" = 0 ] && echo "PASS tests/run_rules_card.sh" || echo "FAIL tests/run_rules_card.sh"
exit $status
