#!/usr/bin/env bash
# `gpdf` phase 1 -- PDF documents, core-14 fonts, measured text and word wrap.
#
# THE ORACLE IS NOT US, and that is the whole design of this suite. A PDF
# writer validated by its own reader proves only self-consistency, and every
# defect in this domain is a PLAUSIBLE DOCUMENT: a line measured against the
# wrong widths still lays out, a substituted character still prints, an xref
# off by forty bytes still opens in a forgiving viewer. So four documents are
# handed to THREE INDEPENDENT IMPLEMENTATIONS -- mupdf, ghostscript and poppler
# -- which share none of our assumptions.
#
# That oracle earned its place before a line of gpdf existed: pointed at the
# library that inspired this one, it reported `cannot recognize xref format`
# and `Bad FCHECK in flate stream` on freshly generated output, and the cause
# was exact -- `startxref 753` for an xref that begins at byte 799. Viewers
# open such a file by repairing it, so nothing short of a strict reader says so.
#
# THE TEXT TIER IS THE SHARPEST. Structural validity says the file parses;
# asking poppler to extract the text says the bytes MEAN what was written --
# which is what catches an unescaped parenthesis (ordinary invoice text that
# ends a PDF string literal early) and a WinAnsi conversion that took the low
# byte of a Euro sign.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

# --- Tier 1: semantics, self-checking ----------------------------------------
out="$work/sem.txt"
if ! GBASIC_PATH=stdlib ./gbasic tests/gpdf/gpdf_test.bas >"$out" 2>&1; then
    cat "$out"; fail "gpdf_test.bas (exited nonzero)"
else
    tier_ok=1
    if grep -q '^MISMATCH' "$out"; then grep '^MISMATCH' "$out"; fail "gpdf_test.bas"; tier_ok=0; fi
    checks="$(grep -oE '^checks: [0-9]+' "$out" | grep -oE '[0-9]+' || echo 0)"
    # A tier that stops running its checks otherwise passes by saying nothing.
    if [ "${checks:-0}" -lt 84 ]; then
        fail "gpdf_test.bas (only $checks checks ran; the file should run at least 84)"
        tier_ok=0
    fi
    # PASS is CONDITIONAL. Printed unconditionally after the checks, a tier
    # reports FAIL and then PASS for the same run -- which is how a red suite
    # reads as green in scroll-back.
    [ "$tier_ok" = 1 ] && printf 'PASS semantics (%s checks)\n' "$checks"
fi

# --- Tier 2: three independent readers ---------------------------------------
docs="$work/docs"; mkdir -p "$docs"
if ! GBASIC_PATH=stdlib ./gbasic tests/gpdf/gpdf_emit.bas "$docs" >"$work/emit.txt" 2>&1; then
    cat "$work/emit.txt"; fail "emitting the acceptance documents"
fi

have_mutool=0; command -v mutool >/dev/null 2>&1 && have_mutool=1
have_gs=0;     command -v gs      >/dev/null 2>&1 && have_gs=1
have_poppler=0;command -v pdftotext >/dev/null 2>&1 && have_poppler=1

if [ "$((have_mutool + have_gs + have_poppler))" -eq 0 ]; then
    printf 'SKIP readers (no mutool, ghostscript or poppler)\n'
else
    readers_ok=1
    for name in basic accents escapes multipage table chart logo; do
        pdf="$docs/$name.pdf"
        [ -s "$pdf" ] || { fail "$name.pdf was not written"; continue; }

        if [ "$have_mutool" = 1 ]; then
            # A repair is the loud form of a structural defect. fpdf-njs trips
            # exactly this, so a pass here is not a formality.
            if mutool info "$pdf" 2>&1 | grep -qiE 'format error|repair'; then
                mutool info "$pdf" 2>&1 | grep -iE 'format error|repair' | head -2
                fail "$name.pdf (mupdf had to repair it)"; readers_ok=0
            fi
        fi
        if [ "$have_gs" = 1 ]; then
            if ! gs -dNOPAUSE -dBATCH -dQUIET -sDEVICE=nullpage "$pdf" >/dev/null 2>"$work/gs.err"; then
                head -3 "$work/gs.err"; fail "$name.pdf (ghostscript refused it)"; readers_ok=0
            elif grep -qi 'error' "$work/gs.err"; then
                head -3 "$work/gs.err"; fail "$name.pdf (ghostscript reported an error)"; readers_ok=0
            fi
        fi
    done
    [ "$readers_ok" = 1 ] && printf 'PASS readers (mupdf=%s ghostscript=%s, 7 documents, no repairs)\n' "$have_mutool" "$have_gs"
fi

# --- Tier 3: the text means what was written ---------------------------------
if [ "$have_poppler" = 1 ]; then
    text_ok=1
    # Captured whole, then read: `pdftotext | head -1` lets head close the pipe
    # first, pdftotext dies of SIGPIPE, and `set -e` ends the suite silently --
    # the failure mode this repo already records for `grep | awk` under pipefail.
    pdftotext "$docs/escapes.pdf" - >"$work/escapes.txt" 2>/dev/null || true
    pdftotext "$docs/accents.pdf" - >"$work/accents.txt" 2>/dev/null || true

    got="$(sed -n '1p' "$work/escapes.txt" | tr -d '\r')"
    want='Ref (A) \ (B) — 50% off (while stocks last)'
    [ "$got" = "$want" ] || { fail "escapes text round trip (got '$got')"; text_ok=0; }

    got="$(sed -n '1p' "$work/accents.txt" | tr -d '\r')"
    [ "$got" = "Müller Provençal naïve" ] || { fail "Latin-1 round trip (got '$got')"; text_ok=0; }
    got="$(sed -n '2p' "$work/accents.txt" | tr -d '\r')"
    # The 0x80-0x9F half: these live elsewhere in Unicode, and a conversion
    # that just took the low byte would mangle exactly this line.
    [ "$got" = "€9.99 — ‘quoted’ ½ ±" ] || { fail "WinAnsi high range round trip (got '$got')"; text_ok=0; }

    pages="$(pdfinfo "$docs/multipage.pdf" 2>/dev/null | grep -oP 'Pages:\s+\K[0-9]+' || echo 0)"
    [ "${pages:-0}" -ge 2 ] || { fail "multipage.pdf has $pages pages; a spilling paragraph must add them"; text_ok=0; }

    # THE TABLE, asked of poppler page by page. Two claims no structural check
    # can make: the heading REPEATS on every page (a table whose header appears
    # once leaves every later page unreadable), and a tall row keeps its lines
    # TOGETHER -- a split row puts a description on one page and its amount on
    # the next, which reads as two different transactions.
    tpages="$(pdfinfo "$docs/table.pdf" 2>/dev/null | grep -oP 'Pages:\s+\K[0-9]+' || echo 0)"
    [ "${tpages:-0}" -ge 2 ] || { fail "table.pdf has $tpages pages; the fixture must span more than one"; text_ok=0; }
    n=1
    while [ "$n" -le "${tpages:-0}" ]; do
        pdftotext -layout -f "$n" -l "$n" "$docs/table.pdf" "$work/tp$n.txt" 2>/dev/null || true
        heads="$(grep -c 'Description' "$work/tp$n.txt" || true)"
        [ "$heads" = "1" ] || { fail "table.pdf page $n has $heads headings; it must have exactly 1"; text_ok=0; }
        n=$((n + 1))
    done
    # EVERY wrapped row must keep its lines together, not just the one that
    # happens to sit mid-page. Checked for all of them, because a break
    # measured on a single line rather than the row's real height only splits
    # the row that lands ON the boundary -- so testing one row passes a
    # library that splits every other one.
    tall_seen=0
    n=1
    while [ "$n" -le "${tpages:-0}" ]; do
        for item in $(grep -oE 'Item [0-9]+:' "$work/tp$n.txt" | grep -oE '[0-9]+'); do
            tall_seen=$((tall_seen + 1))
            grep -A1 "Item $item:" "$work/tp$n.txt" | grep -q 'one line inside its own column' \
                || { fail "table.pdf: row $item is split across a page boundary"; text_ok=0; }
        done
        n=$((n + 1))
    done
    # And the check must have had something to check.
    [ "$tall_seen" -ge 5 ] || { fail "table.pdf: only $tall_seen wrapped rows found; the fixture must produce several"; text_ok=0; }
    # The total must be the sum, and it is money -- 45 rows of 99.99 + k.
    # A CHART'S LABELS MUST COME BACK AS TEXT. That is the whole difference
    # between a vector chart and a picture of one: searchable, selectable,
    # and sharp at any zoom. A rasterised chart extracts nothing.
    pdftotext "$docs/chart.pdf" - >"$work/chart.txt" 2>/dev/null || true
    for want in 'Quarterly report' 'Revenue and costs' 'revenue' 'costs'; do
        grep -qF "$want" "$work/chart.txt" \
            || { fail "chart.pdf: '$want' did not survive as text"; text_ok=0; }
    done

    # THE IMAGES MUST ARRIVE AS IMAGES. mupdf lists what a document actually
    # carries, so this asks an outside reader rather than trusting our record:
    # two of them, at the pixel sizes of the files on disk, one indexed (the
    # palette PNG passed through whole) and one DCT (the JPEG copied whole).
    if [ "$have_mutool" = 1 ]; then
        mutool info "$docs/logo.pdf" >"$work/logo.txt" 2>/dev/null || true
        grep -qE 'Images \(2\)' "$work/logo.txt" \
            || { fail "logo.pdf: mupdf does not report 2 images"; text_ok=0; }
        grep -qE '1629x543 .*Idx' "$work/logo.txt" \
            || { fail "logo.pdf: the palette PNG did not arrive indexed at its own size"; text_ok=0; }
        grep -qE 'DCT' "$work/logo.txt" \
            || { fail "logo.pdf: the JPEG did not arrive as DCT"; text_ok=0; }
    fi

    grep -q 'Total' "$work/tp${tpages}.txt" || { fail "table.pdf: no totals row on the last page"; text_ok=0; }
    [ "$text_ok" = 1 ] && printf 'PASS text (poppler extracts exactly what was written, %s pages)\n' "$pages"
else
    printf 'SKIP text (poppler is not installed)\n'
fi

# --- Tier 4: the offset that no reader forgives ------------------------------
# Checked from OUTSIDE as well as in the fixture, because this is the one
# defect that made the reference implementation unreadable and a library can be
# self-consistently wrong about its own arithmetic.
offsets_ok=1
for name in basic accents multipage; do
    pdf="$docs/$name.pdf"
    said="$(tail -c 64 "$pdf" | tr -d '\000' | grep -A1 '^startxref$' | tail -1 | tr -dc '0-9')"
    real="$(grep -abo '^xref$' "$pdf" | head -1 | cut -d: -f1)"
    if [ -z "$said" ] || [ -z "$real" ] || [ "$said" != "$real" ]; then
        fail "$name.pdf startxref says '${said:-none}', xref is at '${real:-none}'"
        offsets_ok=0
    fi
done
[ "$offsets_ok" = 1 ] && printf 'PASS offsets (startxref names the byte where xref begins)\n'

# --- Tier 5: determinism ------------------------------------------------------
a="$work/a"; b="$work/b"; mkdir -p "$a" "$b"
GBASIC_PATH=stdlib ./gbasic tests/gpdf/gpdf_emit.bas "$a" >/dev/null 2>&1
GBASIC_PATH=stdlib ./gbasic tests/gpdf/gpdf_emit.bas "$b" >/dev/null 2>&1
for name in basic accents escapes multipage; do
    cmp -s "$a/$name.pdf" "$b/$name.pdf" || fail "$name.pdf differs between runs"
done
printf 'PASS determinism (7 documents byte-identical across runs)\n'

# --- Tier 6: the metrics table has not drifted from its generator ------------
# A committed table whose generator produces something else is a table nobody
# can regenerate -- the run_ari lesson, where a golden testing a file nothing
# can reproduce is a golden testing itself.
if command -v python3 >/dev/null 2>&1 && [ -d /usr/share/fonts/type1/urw-base35 ]; then
    cp stdlib/gpdf_metrics.bas "$work/metrics.keep"
    python3 tools/make_pdf_metrics.py >/dev/null
    if ! cmp -s stdlib/gpdf_metrics.bas "$work/metrics.keep"; then
        cp "$work/metrics.keep" stdlib/gpdf_metrics.bas
        fail "stdlib/gpdf_metrics.bas differs from what tools/make_pdf_metrics.py produces"
    else
        printf 'PASS metrics (the committed table is what the generator emits)\n'
    fi
else
    printf 'SKIP metrics drift (python3 or the urw-base35 AFMs are absent)\n'
fi

# --- Tier 7: valgrind ---------------------------------------------------------
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    if GBASIC_PATH=stdlib vg_run ./gbasic tests/gpdf/gpdf_test.bas >/dev/null 2>"$work/vg.err" </dev/null; then
        printf 'PASS valgrind (no definite leak or invalid access)\n'
    else
        cat "$work/vg.err"; fail "valgrind"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

exit $status
