#!/usr/bin/env bash
# stdlib/pdf.bas (docs/pdf_read_design.md; gap G54 from the gBASIC books):
# a PDF's pages, their words with positions, the pictures on them, and OCR of
# a scanned page only when asked.
#
# THE ORACLE IS WHAT THE FIXTURES WROTE, never poppler's answer read back:
# tests/pdf/make_fixtures.bas has gpdf put known words at known places, and a
# picture of that page (rendered by pdftoppm at 100 dpi, so 850 x 1100 pixels)
# on the scanned pages, so a word's text, left edge and baseline and a
# picture's place and pixel size are all known before anything is read.
#
# Tiers:
#   1. READ -- tests/pdf/pdf_test.bas, self-checking: a text page, every page
#      of a three-page document in order (text, blank, text), a scan, a
#      searchable scan (its layer's words, said to be a layer), a text page
#      with a logo (listed, placed, taken out as stored, never read even with
#      ocr: true), two page-sized pictures and pictures smaller than the page
#      (each refused as a value), a file cut short (poppler's words, as a
#      value), and the wrong programs that raise. Floor 74 checks.
#   2. OCR -- the same with OCR: a scan read through the ocr library, its
#      words in the page's points and on gpdf's baselines, and a searchable
#      scan's layer set aside. Floor 88 checks. Skips without tesseract.
#   2b. TURNED -- the fuller page at 300 dpi turned 0, 90, 180 and 270 degrees
#      (book gap G66): each says the turn ocr found, and its words, pictures
#      and size are in the page as read, at places worked out by hand from the
#      margins the fixture gave each side. Skips without tesseract or PIL.
#   3. QUIET -- loading pdf warns nothing (a sibling loaded by name was
#      reported "ALSO FOUND" naming the same file, 2026-11-09).
#   4. VALGRIND, over tier 1.
#
# Skips without poppler-utils (pdftotext, pdfinfo, pdftohtml, pdfimages,
# pdftoppm), saying nothing was read.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
root=$(pwd)

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_pdf: build failed\n'
    exit 1
fi
for t in pdftotext pdfinfo pdftohtml pdfimages pdftoppm; do
    if ! command -v "$t" >/dev/null 2>&1; then
        printf 'SKIP run_pdf (no %s: poppler-utils is not installed; no PDF was read)\n' "$t"
        exit 0
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
export GBASIC_PATH="$root/stdlib"

mkdir -p "$work/fx"
if ! ./gbasic tests/pdf/make_fixtures.bas "$work/fx" text >"$work/make.out" 2>&1 ||
   ! pdftoppm -r 100 -png -singlefile "$work/fx/text.pdf" "$work/fx/page" >>"$work/make.out" 2>&1 ||
   ! pdftoppm -r 300 -png -singlefile "$work/fx/full.pdf" "$work/fx/full" >>"$work/make.out" 2>&1; then
    printf 'FAIL run_pdf: the fixtures could not be made\n'
    cat "$work/make.out"
    exit 1
fi
# the turned scans' pictures (G66), counter-clockwise as PIL counts, written
# at 300 dpi; without PIL there are none and the turned tier says so
have_pil=no
if python3 -I -c 'import PIL' >/dev/null 2>&1 &&
   python3 -I - "$work/fx" >>"$work/make.out" 2>&1 <<'PY'
import sys
from PIL import Image
d = sys.argv[1]
im = Image.open(d + "/full.png")
for a in (0, 90, 180, 270):
    im.rotate(a, expand=True).save("%s/turned%d.png" % (d, a), dpi=(300, 300))
PY
then
    have_pil=yes
fi
if ! ./gbasic tests/pdf/make_fixtures.bas "$work/fx" pictures >>"$work/make.out" 2>&1; then
    printf 'FAIL run_pdf: the fixtures could not be made\n'
    cat "$work/make.out"
    exit 1
fi
# cut inside its objects, which poppler cannot rebuild (cut only past them, it
# rebuilds the trailer and reads the file in full: measured, 900 of 798 bytes)
head -c 400 "$work/fx/text.pdf" >"$work/fx/cut.pdf"

run_tier() {   # label, floor, extra arg
    out=$(timeout 120 ./gbasic tests/pdf/pdf_test.bas "$work/fx" $3 2>"$work/err")
    n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
    if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge "$2" ] && [ ! -s "$work/err" ]; then
        printf 'PASS %s (%s checks)\n' "$1" "$n"
    else
        printf 'FAIL %s (floor %s checks, stderr empty)\n%s\n' "$1" "$2" "$out"
        head -5 "$work/err"
        status=1
    fi
}

printf -- '-- read: text, scans, pictures, refusals\n'
run_tier "read" 74 ""

printf -- '-- ocr: only when asked\n'
if command -v tesseract >/dev/null 2>&1; then
    run_tier "ocr" 88 ocr
else
    printf 'SKIP ocr (no tesseract; no scan was read)\n'
fi

printf -- '-- turned: a scan fed through sideways or upside down\n'
if ! command -v tesseract >/dev/null 2>&1; then
    printf 'SKIP turned (no tesseract; no scan was read)\n'
elif [ "$have_pil" != yes ]; then
    printf 'SKIP turned (no python3 PIL to turn the pictures; no turned scan was read)\n'
else
    run_tier "turned" 126 turned
fi

printf -- '-- quiet: loading pdf warns nothing\n'
printf 'load pdf\nprint pdf.available()\n' >"$work/q.bas"
# the relative spelling is the one that met it: pdf.bas is then stdlib/pdf.bas,
# and its sibling is found beside it, on GBASIC_PATH, and in the tree's stdlib
q=$(GBASIC_PATH=stdlib ./gbasic "$work/q.bas" 2>&1)
if [ "$q" = "true" ]; then
    printf 'PASS loading pdf, which loads ocr by name, warns nothing\n'
else
    printf 'FAIL loading pdf warned:\n%s\n' "$q"
    status=1
fi

if vg_available; then
    if timeout 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/pdf/pdf_test.bas "$1"' _ "$work/fx" >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/pdf/pdf_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
