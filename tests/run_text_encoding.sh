#!/usr/bin/env bash
# Text encodings: transcode(text, from, to), valid_utf8 and first_invalid_utf8
# (gap G10 from the gBASIC books, volume 3).
#
# Tiers:
#   1. ORACLE -- 4120 conversions across utf-8, latin-1, windows-1252,
#      utf-16, utf-16le and utf-16be, a third of them corrupted, each decided
#      by Python's codecs (tests/text/encoding_cases.py): the output bytes, or
#      the offset Python's error starts at. Never this interpreter's answer.
#   2. WHAT THE ORACLE DOES NOT SAMPLE -- utf-16 with no byte-order mark
#      (big-endian, RFC 2781, where Python reads the machine's order), the
#      byte-order mark written, names, and the refusals' wording.
#   3. VALGRIND over the same cases.
#
# Tiers 1 and 3 need python3 and skip, saying so, without it.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
make >/dev/null 2>&1 || { printf 'FAIL run_text_encoding: build failed\n'; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP run_text_encoding: no python3, so no conversion was checked against another implementation\n'
    exit 0
fi
python3 -I tests/text/encoding_cases.py "$work/cases.txt"
if timeout 120 ./gbasic tests/text/encoding_test.bas "$work/cases.txt" >"$work/out" 2>"$work/err" &&
   grep -q '^mismatches: 0$' "$work/out" && [ ! -s "$work/err" ] &&
   [ "$(sed -n 's/^checks: //p' "$work/out")" -ge 4011 ]; then
    printf 'PASS transcode agrees with Python on %s conversions, refusals at the same offsets\n' \
        "$(sed -n 's/^oracle cases: //p' "$work/out")"
    printf 'PASS   and the cases the oracle does not sample (%s checks in all)\n' "$(sed -n 's/^checks: //p' "$work/out")"
else
    printf 'FAIL transcode\n'
    grep -E 'MISMATCH|^checks|^mismatches' "$work/out" | head -10
    head -5 "$work/err"
    status=1
fi
if vg_available; then
    head -400 "$work/cases.txt" >"$work/few.txt"
    if VG_EXTRA="--log-file=$work/vg.log" timeout 300 bash tests/net/vg_wrap.sh ./gbasic tests/text/encoding_test.bas \
           "$work/few.txt" >"$work/vg.out" 2>&1 && grep -q '^mismatches: 0$' "$work/vg.out" &&
       ! grep -q 'Invalid \(read\|write\)\|definitely lost: [1-9]' "$work/vg.log"; then
        printf 'PASS valgrind over 400 conversions and the refusals\n'
    else
        printf 'FAIL valgrind transcode\n'
        grep -B2 -A12 'Invalid\|definitely' "$work/vg.log" | head -30
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi
exit $status
