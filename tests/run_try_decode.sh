#!/usr/bin/env bash
# PLAT-JSON: try_decode(text) -- decode that reports failure as a value.
#
# `decode` raises on malformed input, and gBASIC cannot catch a raise, so every
# caller that reads a file it did not write has had to pre-validate in gBASIC
# first. Any per-character scan in gBASIC is QUADRATIC (`mid(s,i,1)` is O(i) on
# codepoint-indexed strings), so that pre-validation cost 92 s on a 116 KB store.
# This builtin removes the need for it.
#
# Tiers:
#   1. GOLDEN      -- tests/try_decode_test.bas: every valid shape, every malformed
#                     class, round-trip against json_encode, a large document, and
#                     bounded deep nesting.
#   2. PARITY      -- for each malformed input, the error try_decode REPORTS is the
#                     same text decode RAISES. One parser, two ways of answering.
#   3. RAISE INTACT-- decode still raises, and still exits nonzero, unchanged.
#   4. VALGRIND    -- including every malformed path.
#
# Headless, GI-independent, no display, no python3. Runs everywhere.
set -u

cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_try_decode: build failed\n'
    exit 1
fi

stdout_file=$(mktemp)
stderr_file=$(mktemp)
work=$(mktemp -d)
trap 'rm -f "$stdout_file" "$stderr_file"; rm -rf "$work"' EXIT

status=0

# --- Tier 1: golden -----------------------------------------------------------
: >"$stdout_file"
if timeout 300 ./gbasic tests/try_decode_test.bas >"$stdout_file" 2>"$stderr_file" </dev/null; then
    if diff -u tests/try_decode_test.out "$stdout_file"; then
        printf 'PASS tests/try_decode_test.bas\n'
    else
        printf 'FAIL tests/try_decode_test.bas\n'
        status=1
    fi
else
    printf 'FAIL tests/try_decode_test.bas (exit)\n'
    cat "$stderr_file"
    status=1
fi

# --- Tier 2: the reported error equals the raised error ------------------------
# Same parser, same diagnosis. If these ever diverge, one of the two paths has
# grown its own opinion about what is wrong with the input.
parity_ok=1
run_pair() { # json-literal-as-gbasic-string-body
    local body="$1" name="$2"
    printf 'r = try_decode("%s")\nprint r.message\n' "$body" >"$work/t.bas"
    local reported
    reported=$(timeout 60 ./gbasic "$work/t.bas" 2>/dev/null </dev/null)
    printf 'print decode("%s")\n' "$body" >"$work/d.bas"
    local raised
    raised=$(timeout 60 ./gbasic "$work/d.bas" 2>&1 >/dev/null </dev/null | sed 's/.*decode error: //')
    if [ -n "$reported" ] && [ "$reported" = "$raised" ]; then
        printf '  parity %-18s %s\n' "$name" "$reported"
    else
        printf 'FAIL try_decode_parity %s\n  reported=<%s>\n  raised=<%s>\n' "$name" "$reported" "$raised"
        parity_ok=0
    fi
}
run_pair '{\"a\":1'        truncated-object
run_pair '[1,2,'           truncated-array
run_pair '{\"a\":}'        missing-value
run_pair '\"unterminated'  unterminated-string
run_pair '\"a\\qb\"'       bad-escape
run_pair ''                empty-input
run_pair '{\"a\":1}junk'   trailing-text
run_pair '{\"a\" 1}'       missing-colon
run_pair '[,]'             lone-comma
[ "$parity_ok" = 1 ] && printf 'PASS try_decode_parity (reported error == raised error, 9 classes)\n' || status=1

# --- Tier 3: decode's raising behaviour is untouched ---------------------------
# try_decode is strictly additive. decode must still raise, still exit nonzero,
# and still print nothing on stdout.
: >"$stdout_file"
printf 'print decode("{\\"a\\":1")\n' >"$work/raise.bas"
if timeout 60 ./gbasic "$work/raise.bas" >"$stdout_file" 2>"$stderr_file" </dev/null; then
    printf 'FAIL try_decode_raise_intact (decode did not fail)\n'
    status=1
elif grep -q "decode error: expected ',' or '}' at byte 6" "$stderr_file" && [ ! -s "$stdout_file" ]; then
    printf 'PASS try_decode_raise_intact (decode still raises, nonzero, empty stdout)\n'
else
    printf 'FAIL try_decode_raise_intact\n'
    cat "$stderr_file"
    status=1
fi

# A valid document must decode identically through both paths.
printf 'a = decode("{\\"x\\":[1,2,{\\"y\\":true}]}")\nb = try_decode("{\\"x\\":[1,2,{\\"y\\":true}]}")\nprint "same=" + (json_encode(a) = json_encode(b.value))\n' >"$work/same.bas"
if [ "$(timeout 60 ./gbasic "$work/same.bas" 2>/dev/null </dev/null)" = "same=true" ]; then
    printf 'PASS try_decode_same_result (valid input decodes identically both ways)\n'
else
    printf 'FAIL try_decode_same_result\n'
    status=1
fi

# --- Tier 3b: deep nesting cannot crash the interpreter ------------------------
# Unbounded recursion used to segfault around 45 000 levels. Both entry points
# must now refuse cleanly: try_decode as a value, decode as an ordinary raise.
printf 'r = try_decode(repeat("[", 100000))\nprint "ok=" + r.ok\n' >"$work/deep_try.bas"
out=$(timeout 120 ./gbasic "$work/deep_try.bas" 2>/dev/null </dev/null); rc=$?
if [ "$rc" = 0 ] && [ "$out" = "ok=false" ]; then
    printf 'PASS try_decode_deep_value (100k nesting reported, exit 0)\n'
else
    printf 'FAIL try_decode_deep_value (exit %s, out=<%s>)\n' "$rc" "$out"
    status=1
fi
printf 'print decode(repeat("[", 100000))\n' >"$work/deep_raise.bas"
timeout 120 ./gbasic "$work/deep_raise.bas" >/dev/null 2>"$stderr_file" </dev/null; rc=$?
if [ "$rc" = 1 ] && grep -q 'nesting' "$stderr_file"; then
    printf 'PASS try_decode_deep_raise (decode raises instead of segfaulting)\n'
else
    printf 'FAIL try_decode_deep_raise (exit %s, expected 1 with a nesting message)\n' "$rc"
    cat "$stderr_file"
    status=1
fi

# --- Tier 3c: invalid UTF-8 and NUL bytes are data, not a crash ----------------
# The parser is byte-oriented; malformed UTF-8 inside a string must come back as
# bytes rather than being rejected, re-encoded, or truncated at a NUL.
printf 'raw = from_bytes([123,34,107,34,58,34,255,254,34,125])\nr = try_decode(raw)\nprint "ok=" + r.ok\nif r.ok then\n  print "bytes=" + byte_count(r.value.k)\nend if\n' >"$work/utf.bas"
out=$(timeout 60 ./gbasic "$work/utf.bas" 2>/dev/null </dev/null)
if [ "$out" = "ok=true
bytes=2" ]; then
    printf 'PASS try_decode_invalid_utf8 (bytes preserved, no crash)\n'
else
    printf 'FAIL try_decode_invalid_utf8 (out=<%s>)\n' "$out"
    status=1
fi

# --- Tier 4: memory, including the malformed paths -----------------------------
if vg_available; then
    if vg_run ./gbasic tests/try_decode_test.bas >/dev/null 2>"$stderr_file" </dev/null; then
        printf 'PASS try_decode_memory (valgrind clean over every case)\n'
    else
        printf 'FAIL try_decode_memory\n'
        grep -E 'definitely lost|ERROR SUMMARY|Invalid ' "$stderr_file" || tail -30 "$stderr_file"
        status=1
    fi
else
    printf 'SKIP try_decode_memory (valgrind not installed)\n'
fi

exit "$status"
