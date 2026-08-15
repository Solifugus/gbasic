#!/usr/bin/env bash
# Build examples/fixtures/xlsx/textmath.xlsx -- the TEXT and MATH family fixture.
#
# WHY THIS EXISTS, and why it was not built earlier. The evaluator grew by
# following measurement, and the measurement was wrong about this for months:
# `xlsx.check` counted `unsupported` cells but never recorded WHICH function it
# had refused, so the only available ranking was counting `NAME(` tokens in
# formula text -- the method §13.J showed to be structurally blind. Once notes
# carried `blocked_by` (2026-08-15) and the corpus was re-ranked, the top of the
# list was not the lookup and aggregate work that was next on the roadmap. It
# was FIND (240,587 blocked cells) and LEFT (207,757), with LN, EXP, SQRT, MID
# and HOUR close behind: ordinary text and math functions that had simply never
# been written.
#
# WHY LIBREOFFICE GENERATES IT. Same reason as modern.xlsx: the cached values in
# an xlsx are what make it an oracle, and hand-writing them only proves
# self-consistency -- if I misunderstand Excel's TRIM I will write the wrong
# expected value and the test will happily agree with me. Having an independent
# spreadsheet engine compute them removes exactly that failure mode. The caveat
# is unchanged and real: LibreOffice is not Excel, so this is strong evidence
# rather than proof, and the ranking stays
#
#     Excel-authored (the corpus)  >  LibreOffice-authored (this)  >  hand-written
#
# For these two families the corpus is ALSO available as an oracle, and is the
# stronger one -- hundreds of thousands of real FIND and LEFT cells carrying
# Excel's own answers. This fixture is the committed, deterministic, offline
# tier; the corpus re-run is the one that decides whether the implementations
# are right.
#
# The cases are chosen where a plausible-but-wrong implementation differs from
# the correct one, not merely to exercise each name:
#   * MID/LEFT past the end of the string, and MID with start < 1 (an ERROR in
#     Excel, not a clamp).
#   * FIND absent -> #VALUE!, not 0, which is why callers wrap it in IFERROR.
#   * FIND case-sensitive vs SEARCH case-insensitive on the SAME input, so an
#     implementation that folded both would fail one.
#   * TRIM with interior runs, which Excel COLLAPSES -- a plain strip passes a
#     leading/trailing test and fails this.
#   * INT and TRUNC on a NEGATIVE, where floor and truncate disagree.
#   * MOD with a negative operand, where Excel follows the divisor's sign and C
#     fmod does not.
#   * CEILING/FLOOR to a non-integer multiple, which ceil/floor would ignore.
#   * SUMPRODUCT over two ranges.
#   * HOUR/MINUTE/SECOND on a serial whose fraction does not land exactly on a
#     second boundary.
#
# The .xlsx is COMMITTED, so the suite never runs LibreOffice. This script is
# only how it was authored.
#
# Usage: tools/make_xlsx_textmath_fixture.sh
set -eu
cd "$(dirname "$0")/.."

command -v libreoffice >/dev/null 2>&1 || { echo "needs libreoffice"; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
csv="$work/textmath.csv"

# Columns: A text  B number  C aux   D label  E formula
printf 'text\tnumber\taux\tlabel\tformula\n'                    >"$csv"
printf '  Loan A  East \t-2.5\t3\tLEN\t=LEN(A2)\n'              >>"$csv"
printf 'Widget-42\t2.1\t4\tLEFT\t=LEFT(A3,6)\n'                 >>"$csv"
printf 'east WEST east\t7\t5\tRIGHT\t=RIGHT(A4,4)\n'            >>"$csv"
printf 'abc\t-3\t6\tMID\t=MID(A3,8,2)\n'                        >>"$csv"

add() { printf '\t\t\t%s\t%s\n' "$1" "$2" >>"$csv"; }

# --- text -------------------------------------------------------------------
add 'LEN of trimmed'   '=LEN(TRIM(A2))'
add 'LEFT default'     '=LEFT(A3)'
add 'LEFT past end'    '=LEFT(A5,99)'
add 'RIGHT past end'   '=RIGHT(A5,99)'
add 'MID past end'     '=MID(A5,10,3)'
add 'MID zero len'     '=MID(A3,2,0)'
add 'MID start err'    '=IFERROR(MID(A3,0,2),"err")'
add 'FIND hit'         '=FIND("-",A3)'
add 'FIND from'        '=FIND("east",A4,2)'
add 'FIND case'        '=IFERROR(FIND("EAST",A4),"not-found")'
add 'SEARCH case'      '=SEARCH("EAST",A4)'
add 'FIND absent'      '=IFERROR(FIND("zz",A3),"not-found")'
add 'TRIM interior'    '=TRIM(A2)'
add 'TRIM len'         '=LEN(TRIM("  a   b  "))'
add 'SUBSTITUTE all'   '=SUBSTITUTE(A4,"east","E")'
add 'SUBSTITUTE nth'   '=SUBSTITUTE(A4,"east","E",2)'
add 'REPLACE'          '=REPLACE(A3,1,6,"Gadget")'
add 'REPT'             '=REPT("ab",3)'
add 'UPPER'            '=UPPER(A3)'
add 'LOWER'            '=LOWER(A3)'
add 'PROPER'           '=PROPER(A4)'
add 'EXACT same'       '=EXACT("abc","abc")'
add 'EXACT case'       '=EXACT("abc","ABC")'
add 'CHAR'             '=CHAR(65)'
add 'CODE'             '=CODE("A")'
add 'VALUE'            '=VALUE("1234.5")'
add 'T of text'        '=T(A3)'
add 'T of number'      '=T(B3)'
add 'FIND into MID'    '=MID(A3,FIND("-",A3)+1,2)'

# --- math -------------------------------------------------------------------
add 'SQRT'             '=SQRT(C4)'
add 'SQRT neg'         '=IFERROR(SQRT(-1),"num-err")'
add 'EXP'              '=EXP(1)'
add 'LN'               '=LN(10)'
add 'LN zero'          '=IFERROR(LN(0),"num-err")'
add 'LOG10'            '=LOG10(1000)'
add 'LOG default'      '=LOG(1000)'
add 'LOG base 2'       '=LOG(8,2)'
add 'POWER'            '=POWER(2,10)'
add 'INT positive'     '=INT(2.7)'
add 'INT negative'     '=INT(-1.5)'
add 'TRUNC negative'   '=TRUNC(-1.5)'
add 'TRUNC digits'     '=TRUNC(3.14159,2)'
add 'MOD positive'     '=MOD(7,3)'
add 'MOD negative'     '=MOD(-3,2)'
add 'MOD neg divisor'  '=MOD(3,-2)'
add 'MOD zero'         '=IFERROR(MOD(1,0),"div0")'
add 'SIGN'             '=SIGN(B2)'
add 'PI'               '=PI()'
# LibreOffice rewrites a bare CEILING/FLOOR into the modern _xlfn.CEILING.MATH
# and _xlfn.FLOOR.MATH spellings on export, so both forms are covered -- and the
# .MATH variants have a DIFFERENT signature (significance is optional, and a
# third `mode` argument controls how negatives round), which is why they are
# implemented separately rather than aliased.
add 'CEILING'          '=CEILING(2.1,0.5)'
add 'FLOOR'            '=FLOOR(2.9,0.5)'
# NOT included: CEILING/FLOOR on a NEGATIVE. LibreOffice exports those as
# _xlfn.CEILING.MATH and then cannot evaluate its own output -- it caches
# #VALUE! -- so it cannot serve as an oracle for them and a golden would pin an
# artifact. The negative-mode behaviour IS implemented (CEILING.MATH(-2.1) is -2
# toward zero by default, -3 with a non-zero mode; FLOOR.MATH mirrors it) but is
# implemented from Microsoft's documentation and is NOT independently validated
# by anything here. Stated rather than glossed: it is the one part of this
# fixture's subject matter with no second opinion behind it.
add 'PRODUCT'          '=PRODUCT(C2:C5)'
add 'SUMPRODUCT'       '=SUMPRODUCT(B2:B5,C2:C5)'
add 'ABS'              '=ABS(B2)'

# --- clock parts ------------------------------------------------------------
# Deliberately fed through TIME() rather than a decimal literal.
#
# The first version used the literal 0.5645833333 (13:33:00 to ten places) and
# LibreOffice answered HOUR=13, MINUTE=32, SECOND=0 -- which is 13:32:00, and is
# not what ANY single rounding of that input produces: 48779.999997 seconds
# truncates to 13:32:59 and rounds to 13:33:00. Its three answers are mutually
# inconsistent, so it is the outlier here rather than the authority, and pinning
# a golden to it would enshrine a quirk of one engine.
#
# The evaluator rounds the whole serial to the nearest second and then splits
# it, which is self-consistent and is what keeps a stored 13:33:00 -- held as a
# binary fraction that is really ...29.9999997 -- from reading back as 13:32:59.
# The real adjudicator for that choice is the corpus: 27,518 HOUR cells carrying
# Excel's own cached answers, checked by the corpus re-run rather than here.
add 'HOUR'             '=HOUR(TIME(13,33,0))'
add 'MINUTE'           '=MINUTE(TIME(13,33,0))'
add 'SECOND'           '=SECOND(TIME(13,33,0))'
add 'MINUTE mid'       '=MINUTE(TIME(6,7,8))'
add 'SECOND mid'       '=SECOND(TIME(6,7,8))'
add 'TIME'             '=TIME(13,33,0)'
add 'HOUR of TIME'     '=HOUR(TIME(23,59,59))'
add 'SECOND of TIME'   '=SECOND(TIME(23,59,59))'

libreoffice --headless --infilter="CSV:9,34,76,1" --convert-to xlsx "$csv" \
    --outdir "$work" >/dev/null 2>&1

out=examples/fixtures/xlsx/textmath.xlsx
mkdir -p "$(dirname "$out")"
cp "$work/textmath.xlsx" "$out"
printf 'wrote %s (%s bytes)\n' "$out" "$(stat -c%s "$out")"
