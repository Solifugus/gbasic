#!/usr/bin/env bash
# PLAT-RENDER: one renderer turns a value into text.
#
# `print` carried its own switch, separate from the one `string()` uses. It
# understood numbers inside an array and nothing else:
#
#     print(["a", "b"])      ->  [?, ?]              string() ->  ["a","b"]
#     print([[1], [2]])      ->  [?, ?]              string() ->  [[1],[2]]
#     print({ a: 1 })        ->  {record}            string() ->  {"a":1}
#     print(2 days 3 hours)  ->  {duration}          string() ->  {duration}
#
# So a record could not be displayed at all -- `print` emitted the literal word
# `{record}` -- which is the single most common thing a person wants while
# debugging. Nothing failed, because the goldens had captured the broken output
# AS the expected output: examples/record_helpers_test.out and
# examples/conversion_builtin_test.out contained `[?, ?]` and `{record}` and
# were actively defending them.
#
# The irony is that the file already knew the argument. The comment above
# value_print_to explains that `print` and `print to error` share one renderer
# precisely so they cannot drift apart on some value shape. That reasoning was
# right and had simply not been applied one level out.
#
# `print` now delegates to builtin_string_value. Three things had to be true for
# that to be safe, and each is a tier below:
#
#   * DISPLAY MUST BE TOTAL. string() reached compound values through the JSON
#     encoder, which legitimately refuses a date or a function -- so
#     `string([aDate])` RAISED. Delegating naively would have made `print` able
#     to kill the program. The walker now has three modes (RENDER_JSON,
#     RENDER_ENCODE, RENDER_DISPLAY) and display recurses into the single-value
#     renderer for typed kinds, which has a text form for every one.
#   * ENCODE MUST BE UNCHANGED. `encode` and `json_encode` still refuse typed
#     and live values; a lossy token would produce text that does not round-trip
#     through `decode`. Pinned as negatives, since a leak of the display mode
#     into that path would be silent.
#   * DURATIONS NEEDED A TEXT FORM. After the unification, duration was the one
#     kind a program could hold and not display -- still the literal
#     `{duration}` in both renderers. It now renders in the words the literal
#     syntax uses (`2 days 3 hours`), largest component first, singular at 1.
#
# Tiers:
#   1. PARITY   -- the fixture runs TWICE over the same value list, once through
#      `print v` and once through `print string(v)`, and the two captures must be
#      byte-identical. Structural, not a transcript: it cannot drift, because a
#      change to either renderer moves one side only. This is the tier that would
#      have caught the original defect.
#   2. GOLDEN   -- the rendering itself, so a change to how any shape prints is
#      visible in a diff rather than hidden behind a passing boolean.
#   3. TOTALITY -- display never raises. The fixture ends with typed values
#      NESTED inside arrays and records, every one of which raised before.
#   4. BOUNDARY -- encode/json_encode still refuse those same values.
#   5. VALGRIND -- display mode allocates per nested typed value (it recurses
#      into a renderer that returns an owned string), which is a new allocation
#      path on every print.
#
# Headless, GI-independent, no display. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_render: build failed\n'
    exit 1
fi

err=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$err"; rm -rf "$work"' EXIT

status=0

# --- Tier 1 + 3: parity, and the fact that neither run raises -----------------
if ! timeout 300 ./gbasic tests/render_parity_test.bas >"$work/print.txt" 2>"$err" </dev/null; then
    printf 'FAIL render_parity_test.bas via `print` (exit) -- display raised:\n'
    cat "$err"
    status=1
elif ! timeout 300 ./gbasic tests/render_parity_test.bas string >"$work/string.txt" 2>"$err" </dev/null; then
    printf 'FAIL render_parity_test.bas via `string()` (exit) -- display raised:\n'
    cat "$err"
    status=1
else
    printf 'PASS totality (neither renderer raised, incl. typed values nested in compounds)\n'
    if diff -u "$work/print.txt" "$work/string.txt" >"$work/parity.diff"; then
        lines=$(wc -l <"$work/print.txt" | tr -d ' ')
        # Coverage floor: an empty or truncated capture would make the diff pass
        # by comparing nothing to nothing.
        if [ "$lines" -ge 50 ]; then
            printf 'PASS parity (%s value shapes render identically through print and string())\n' "$lines"
        else
            printf 'FAIL parity: only %s lines rendered, expected at least 50 (fixture truncated?)\n' "$lines"
            status=1
        fi
    else
        printf 'FAIL parity -- `print v` and `print string(v)` disagree:\n'
        cat "$work/parity.diff"
        status=1
    fi
fi

# --- Tier 2: golden -----------------------------------------------------------
if [ -s "$work/print.txt" ]; then
    if diff -u tests/render_parity_test.out "$work/print.txt"; then
        printf 'PASS tests/render_parity_test.bas (golden)\n'
    else
        printf 'FAIL tests/render_parity_test.bas -- rendering moved\n'
        status=1
    fi
fi

# --- Tier 3b: the specific shapes that were broken ----------------------------
# Named individually so a regression reports WHICH shape went back, rather than
# only that the golden moved.
expect_line() { # description literal
    if grep -qxF "$2" "$work/print.txt"; then
        printf 'PASS renders %-26s %s\n' "$1" "$2"
    else
        printf 'FAIL renders %-26s expected a line exactly: %s\n' "$1" "$2"
        status=1
    fi
}
expect_line "string array"      '["a","b"]'
expect_line "nested array"      '[[1],[2]]'
expect_line "record"            '{"a":1,"b":"two"}'
expect_line "empty record"      '{}'
expect_line "nested record"     '{"nested":{"deep":[1,2]}}'
expect_line "duration"          '2 days 3 hours'
expect_line "singular duration" '1 day'
expect_line "zero duration"     '0 seconds'
expect_line "date in an array"  '[2026-05-15]'
expect_line "record of typed"   '{"when":2026-05-15,"cost":19.95}'
# The `?` placeholder must be gone entirely. A bare grep for '?' would also match
# a legitimate '?' inside string data, so this looks for the array form.
if grep -qE '\[\?|, \?' "$work/print.txt"; then
    printf 'FAIL the `?` placeholder is still being emitted:\n'
    grep -nE '\[\?|, \?' "$work/print.txt"
    status=1
else
    printf 'PASS no `?` placeholder anywhere in the output\n'
fi

# --- Tier 4: the encode boundary ----------------------------------------------
# Display became total; encode must NOT have. If the display mode leaked into
# the JSON path, encode would emit a token that decode cannot read back, and
# nothing else in the suite would notice.
boundary() { # call expected-message-fragment
    printf 'program main(args)\n  d(date)= "2026-05-15"\n  print(%s)\nend program\n' "$1" >"$work/b.bas"
    if ./gbasic "$work/b.bas" >"$work/b.out" 2>"$work/b.err"; then
        printf 'FAIL boundary: %s did NOT refuse -- it emitted: %s\n' "$1" "$(cat "$work/b.out")"
        status=1
    elif grep -q "$2" "$work/b.err"; then
        printf 'PASS boundary %-28s still refuses a typed value\n' "$1"
    else
        printf 'FAIL boundary: %s raised the wrong error:\n' "$1"
        cat "$work/b.err"
        status=1
    fi
}
boundary 'encode({ when: d })'      'encode supports numbers'
boundary 'json_encode({ when: d })' 'json_encode supports numbers'

# --- Tier 5: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full \
                --errors-for-leak-kinds=definite \
                ./gbasic tests/render_parity_test.bas >"$work/vg.txt" 2>"$err" </dev/null; then
        if diff -q tests/render_parity_test.out "$work/vg.txt" >/dev/null; then
            printf 'PASS valgrind tests/render_parity_test.bas\n'
        else
            printf 'FAIL valgrind tests/render_parity_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/render_parity_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
