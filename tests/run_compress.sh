#!/usr/bin/env bash
# `compress` / `uncompress` -- deflate, reachable from a gBASIC program.
#
# zlib has been linked since the xlsx engine shipped and was reachable ONLY
# from inside it, so a pure-gBASIC program could not deflate a byte. Found
# while checking whether a PDF writer was feasible: /FlateDecode is a zlib
# stream, and nothing could produce one.
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced -- compressed output is opaque
# bytes, so a golden records whatever came out and defends it. Every defect
# here produces a PLAUSIBLE BLOB: a stream in the wrong container, a level
# silently ignored, a length read with strlen so the tail is dropped. None of
# them looks wrong until something else tries to read it.
#
# THE LOAD-BEARING TIER IS THAT THE FORMAT IS A DIFFERENCE. "Each format round
# trips" is satisfied by an implementation that IGNORES `format` and always
# uses zlib -- it would compress and decompress consistently and pass every
# round trip. So the suite asserts the containers are distinguishable on the
# wire (0x78, 1f8b, and raw shortest for carrying no header) AND that a stream
# read as the wrong one is REFUSED rather than half-decoded. That matters
# because the three are not interchangeable and the failure is silent in C:
# PDF's /FlateDecode wants zlib, a ZIP member wants raw.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null


out="$(mktemp)"; trap 'rm -f "$out"' EXIT

if ! GBASIC_PATH=stdlib ./gbasic tests/compress_test.bas >"$out" 2>&1; then
    cat "$out"
    printf 'FAIL tests/compress_test.bas (exited nonzero)\n'
    exit 1
fi

if grep -q 'not available in this build' "$out"; then
    printf 'SKIP tests/compress_test.bas (built without zlib)\n'
    exit 0
fi

if grep -q '^MISMATCH' "$out"; then
    grep '^MISMATCH' "$out"
    printf 'FAIL tests/compress_test.bas\n'
    exit 1
fi

# A tier that stops running its checks otherwise passes by saying nothing.
checks="$(grep -oE '^checks: [0-9]+' "$out" | grep -oE '[0-9]+' || echo 0)"
if [ "${checks:-0}" -lt 28 ]; then
    printf 'FAIL tests/compress_test.bas (only %s checks ran; the file should run at least 28)\n' "$checks"
    exit 1
fi
printf 'PASS tests/compress_test.bas (%s checks)\n' "$checks"

# VALGRIND. Both paths allocate and grow a buffer, and the refusal paths free
# it on the way out -- a leak or an invalid read here produces no wrong value,
# which is exactly the class the functional tier cannot see.
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    vgerr="$(mktemp)"
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/compress_test.bas >/dev/null 2>"$vgerr" </dev/null; then
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    else
        cat "$vgerr"
        rm -f "$vgerr"
        printf 'FAIL valgrind tests/compress_test.bas\n'
        exit 1
    fi
    rm -f "$vgerr"
else
    printf 'SKIP valgrind (unavailable)\n'
fi
