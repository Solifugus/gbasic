#!/usr/bin/env bash
# `ocr` -- reading text out of an image (docs/ocr_design.md, stdlib/ocr.bas).
#
# PURE gBASIC OVER THE TESSERACT CLI, which is a decision: a native module needs
# libtesseract at build time and the lean release tarball carries no optional
# modules, so a native `ocr` would be ABSENT FROM THE DOWNLOAD A READER GETS.
# This works anywhere tesseract is installed. Every measurement behind the
# design came out of the CLI's own TSV.
#
# SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is a PLAUSIBLE
# DOCUMENT. A grid whose rows are off by one still reads like a report, and the
# 5-degree case does not produce a broken row -- it produces a WELL-FORMED one
# carrying another row's money, at the same confidence as a clean page. A golden
# would record that and defend it.
#
# THE LOAD-BEARING TIER IS THE GRID, and it is asserted PER ROW with a CONTROL:
# "a grid came back" is satisfied by one that put every word on a single line,
# and three correct rows can be luck, so each account must sit with ITS OWN
# amount AND 781 must NOT sit with 782's.
#
# SKIPS RATHER THAN FAILS without tesseract -- it is not a build dependency, and
# a gate that reports a fact about the machine as a defect teaches people to
# ignore it. But the skip SAYS SO, since a silent skip is the other way a gate
# shrinks.
set -u
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
export GBASIC_PATH="$PWD/stdlib"
status=0

if ! command -v tesseract >/dev/null 2>&1; then
    echo "SKIP run_ocr: tesseract is not installed (apt install tesseract-ocr)"
    exit 0
fi

printf 'TIER semantics\n'
out="$(timeout 600 ./gbasic tests/ocr/ocr_test.bas 2>&1)"
if printf '%s' "$out" | grep -q '^SKIP'; then
    printf '  %s\n' "$(printf '%s' "$out" | grep '^SKIP')"
    exit 0
fi
if printf '%s' "$out" | grep -q '^mismatches: 0$'; then
    n="$(printf '%s' "$out" | sed -n 's/^checks: //p')"
    printf '  ok   %s checks, 0 mismatches\n' "$n"
    [ "${n:-0}" -ge 15 ] || { printf '  FAIL too few checks ran (%s)\n' "$n"; status=1; }
else
    printf '  FAIL ocr_test\n'
    printf '%s\n' "$out" | grep -E '^MISMATCH|^runtime error|^parse error' | head -6
    status=1
fi

# THE SEAM, ASSERTED SEPARATELY so a failure names which half broke. What `grid`
# produces is a print-image report, which is what `ari` already parses -- the
# design's whole architectural claim, and the reason OCR is a front door to
# existing machinery rather than a new pipeline.
printf 'TIER seam\n'
if printf '%s' "$out" | grep -q 'ari_discover finds the 3-row detail family'; then
    printf '  ok   a page that began as a PNG profiles as a report\n'
else
    printf '  FAIL the OCR -> ari_discover seam did not hold\n'
    status=1
fi

# THE DEPENDENCY IS NAMED, because the failure it produces says nothing:
# reading a script with the wrong language data returns WORDS at 30-45%
# confidence, not an error and not an empty page, and a pipeline checking "did
# OCR return text" passes. Measured -- installing the language took the same
# pages from 3 to 117 high-confidence words.
printf 'TIER languages\n'
langs="$(tesseract --list-langs 2>&1 | tail -n +2 | tr '\n' ' ')"
if printf '%s' "$langs" | grep -qw eng; then
    printf '  ok   eng is installed (%s)\n' "$(printf '%s' "$langs" | tr -s ' ')"
else
    printf '  FAIL no eng traineddata; every page would read as plausible nonsense\n'
    status=1
fi

echo
if [ "$status" = 0 ]; then echo "run_ocr: all cases passed"; fi
exit "$status"
