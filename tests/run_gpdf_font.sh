#!/usr/bin/env bash
# gpdf embedded fonts (book gap G58): a TrueType font read from its file and
# put in the PDF subset to the glyphs drawn, as a Type0/CIDFontType2 font with
# a ToUnicode map, so a statement can name a customer in Łódź.
#
# THE ORACLE IS THE ORIGINAL FONT FILE, read by fontTools. The fixture font is
# made per run (tests/gpdf/make_font.py), so its glyphs, advances and
# composites are known before anything is drawn; the PDF is rewritten by mupdf
# before it is read, and every drawn glyph's outline, advance, bearing and /W
# width is held to the original's (tests/gpdf/check_font.py).
#
# Tiers:
#   1. SEMANTICS -- tests/gpdf/font_test.bas: reading, measuring by hand-summed
#      advances, representable, every refusal (a missing glyph, a script that
#      needs shaping, a control character, CFF outlines, a licence forbidding
#      embedding, a collection index, a file cut short), the subset tag, and
#      Info strings in PDFDocEncoding or UTF-16. Floor 53 checks, stderr empty.
#   2. READERS -- mupdf, ghostscript and poppler open every document with no
#      repair or error; pdffonts calls each embedded font CID TrueType,
#      embedded, subset and with a ToUnicode map.
#   3. TEXT -- pdftotext gives back exactly the lines font_lines.json asked
#      for, and the table, paragraph and page numbers set in the font.
#   4. ORACLE -- check_font.py over the fixture document and, when the machine
#      has DejaVu Sans, a document in Polish, Turkish, Russian, Greek, Czech,
#      Hungarian and Romanian set in it.
#   5. CMAP -- the glyph gpdf finds for each of 12295 codepoints (all of
#      U+0000-U+2FFF and a sample past the BMP) is fontTools' answer, through
#      format 12 (the fixture, DejaVu) and format 4 with idRangeOffset
#      segments (a fixture without U+1D400, and DejaVu with its format 12
#      records pointed at its format 4 bytes, since a fontTools re-save
#      re-encodes format 4 without them).
#   6. DETERMINISM -- the documents are byte-identical across two runs.
#   7. VALGRIND, over tier 1.
#
# Skips without python3's fontTools (no fixture font can be made).
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
export GBASIC_PATH=stdlib

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_gpdf_font: build failed\n'
    exit 1
fi
if ! python3 -I -c 'import fontTools' >/dev/null 2>&1; then
    printf 'SKIP run_gpdf_font (no python3 fontTools: no fixture font could be made, nothing was embedded)\n'
    exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

mkdir -p "$work/fx" "$work/a" "$work/b"
dejavu=none
for p in /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/TTF/DejaVuSans.ttf \
         /usr/local/share/fonts/dejavu/DejaVuSans.ttf /usr/share/fonts/dejavu/DejaVuSans.ttf; do
    if [ -f "$p" ]; then dejavu=$p; break; fi
done
sysarg=""
[ "$dejavu" != none ] && sysarg=$dejavu
if ! python3 -I tests/gpdf/make_font.py "$work/fx" $sysarg >"$work/make.out" 2>&1; then
    printf 'FAIL run_gpdf_font: the fixture fonts could not be made\n'
    cat "$work/make.out"
    exit 1
fi

printf -- '-- semantics\n'
out=$(timeout 120 ./gbasic tests/gpdf/font_test.bas "$work/fx" 2>"$work/err")
n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge 53 ] && [ ! -s "$work/err" ]; then
    printf 'PASS semantics (%s checks)\n' "$n"
else
    printf '%s\n' "$out" | grep -v '^ok ' | head -20
    head -5 "$work/err"
    fail "semantics (floor 53 checks, stderr empty)"
fi

if ! timeout 120 ./gbasic tests/gpdf/font_emit.bas "$work/a" "$work/fx" "$dejavu" >"$work/emit.out" 2>&1; then
    cat "$work/emit.out"
    fail "emitting the documents"
    exit 1
fi
docs="fixture table pair"
[ "$dejavu" != none ] && docs="$docs dejavu"

printf -- '-- readers\n'
have_mutool=0; command -v mutool >/dev/null 2>&1 && have_mutool=1
have_gs=0;     command -v gs      >/dev/null 2>&1 && have_gs=1
have_poppler=0; command -v pdffonts >/dev/null 2>&1 && command -v pdftotext >/dev/null 2>&1 && have_poppler=1
readers_ok=1
for d in $docs; do
    pdf="$work/a/$d.pdf"
    if [ "$have_mutool" = 1 ] && mutool info "$pdf" 2>&1 | grep -qiE 'format error|repair|cannot'; then
        mutool info "$pdf" 2>&1 | grep -iE 'format error|repair|cannot' | head -3
        fail "$d.pdf (mupdf had to repair it)"; readers_ok=0
    fi
    if [ "$have_gs" = 1 ]; then
        if ! gs -dNOPAUSE -dBATCH -dQUIET -sDEVICE=nullpage "$pdf" >"$work/gs.out" 2>&1 || grep -qi 'error' "$work/gs.out"; then
            head -3 "$work/gs.out"; fail "$d.pdf (ghostscript reported an error)"; readers_ok=0
        fi
    fi
    if [ "$have_poppler" = 1 ]; then
        pdffonts "$pdf" >"$work/fonts.txt" 2>"$work/fonts.err" || true
        # name, type (two words), encoding, emb, sub, uni
        if ! grep -qE '^[A-Z]{6}\+[A-Za-z0-9_-]+ +CID TrueType +Identity-H +yes +yes +yes ' "$work/fonts.txt" || [ -s "$work/fonts.err" ]; then
            cat "$work/fonts.txt" "$work/fonts.err"
            fail "$d.pdf (pdffonts: not an embedded, subset CID TrueType font with a ToUnicode map)"; readers_ok=0
        fi
    fi
done
if [ "$have_poppler" = 1 ] && ! grep -qE '\+FixtureTwo ' <(pdffonts "$work/a/pair.pdf" 2>/dev/null); then
    fail "pair.pdf does not hold the collection's second font"; readers_ok=0
fi
[ "$readers_ok" = 1 ] && printf 'PASS readers (mupdf=%s ghostscript=%s poppler=%s; %s)\n' \
    "$have_mutool" "$have_gs" "$have_poppler" "$docs"

printf -- '-- text\n'
if [ "$have_poppler" = 1 ]; then
    text_ok=1
    # what was asked for, read from font_lines.json by python3, not by gBASIC
    for key in fixture dejavu; do
        [ "$key" = dejavu ] && [ "$dejavu" = none ] && continue
        python3 -I -c 'import json,sys; [print(l["text"]) for l in json.load(open(sys.argv[1], encoding="utf-8"))[sys.argv[2]]]' \
            tests/gpdf/font_lines.json "$key" >"$work/want.txt"
        pdftotext "$work/a/$key.pdf" - 2>/dev/null | grep -v '^$' | tr -d '\f' | grep -v '^$' >"$work/got.txt" || true
        if [ "$key" = fixture ]; then
            # the Helvetica line drawn after the first, and page two's word
            { sed -n 1p "$work/want.txt"; echo "Helvetica between"; sed -n '2,$p' "$work/want.txt"; echo "Łódź"; } >"$work/want2.txt"
            mv "$work/want2.txt" "$work/want.txt"
        fi
        if ! cmp -s "$work/want.txt" "$work/got.txt"; then
            diff "$work/want.txt" "$work/got.txt" | head -10
            fail "$key.pdf: pdftotext does not give back the lines asked for"; text_ok=0
        fi
    done
    pdftotext -layout "$work/a/table.pdf" - 2>/dev/null >"$work/table.txt" || true
    for want in 'Zakład Mechaniczny Łódź' 'Łódź +123' 'Zakład +321' 'Łódź 1'; do
        grep -qE "$want" "$work/table.txt" || { fail "table.pdf: '$want' is not in the text"; text_ok=0; }
    done
    title=$(pdfinfo "$work/a/fixture.pdf" 2>/dev/null | sed -n 's/^Title: *//p')
    [ "$title" = "Łódź statement" ] || { fail "fixture.pdf title reads '$title'"; text_ok=0; }
    [ "$text_ok" = 1 ] && printf 'PASS text (poppler gives back what was asked for, title included)\n'
else
    printf 'SKIP text (poppler is not installed)\n'
fi

printf -- '-- oracle: fontTools holds each glyph to the original font\n'
if [ "$have_mutool" = 1 ]; then
    for key in fixture dejavu; do
        font="$work/fx/fixture.ttf"
        if [ "$key" = dejavu ]; then
            [ "$dejavu" = none ] && { printf 'SKIP oracle dejavu (no DejaVu Sans on this machine)\n'; continue; }
            font=$dejavu
        fi
        if o=$(python3 -I tests/gpdf/check_font.py "$work/a/$key.pdf" "$font" tests/gpdf/font_lines.json "$key" 2>&1); then
            printf 'PASS oracle %s (%s)\n' "$key" "$(printf '%s\n' "$o" | sed -n 's/^checks: //p') checks"
        else
            printf '%s\n' "$o" | head -15
            fail "oracle $key"
        fi
    done
else
    printf 'SKIP oracle (no mutool to rewrite the documents before reading them)\n'
fi

printf -- '-- cmap: every codepoint against fontTools\n'
cmaps="$work/fx/fixture.ttf:12 $work/fx/bmp.ttf:4"
[ "$dejavu" != none ] && cmaps="$cmaps $dejavu:12 $work/fx/system4.ttf:4"
for pair in $cmaps; do
    font=${pair%:*}; want=${pair##*:}
    if timeout 120 ./gbasic tests/gpdf/cmap_dump.bas "$font" >"$work/dump.txt" 2>"$work/dump.err" && [ ! -s "$work/dump.err" ] &&
       o=$(python3 -I tests/gpdf/check_cmap.py "$font" "$work/dump.txt" "$want" 2>&1); then
        printf 'PASS cmap format %s %s (%s)\n' "$want" "$(basename "$font")" "$(printf '%s\n' "$o" | head -1)"
    else
        printf '%s\n' "${o:-}" | head -12; head -3 "$work/dump.err"
        fail "cmap format $want $(basename "$font")"
    fi
done
[ "$dejavu" = none ] && printf 'SKIP cmap of a real font (no DejaVu Sans on this machine)\n'

printf -- '-- determinism\n'
timeout 120 ./gbasic tests/gpdf/font_emit.bas "$work/b" "$work/fx" "$dejavu" >/dev/null 2>&1
det_ok=1
for d in $docs; do
    cmp -s "$work/a/$d.pdf" "$work/b/$d.pdf" || { fail "$d.pdf differs between runs"; det_ok=0; }
done
[ "$det_ok" = 1 ] && printf 'PASS determinism (%s byte-identical across runs)\n' "$docs"

if vg_available; then
    if timeout 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/gpdf/font_test.bas "$1"' _ "$work/fx" >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/gpdf/font_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
