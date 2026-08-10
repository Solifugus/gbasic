#!/usr/bin/env bash
# Build examples/fixtures/xlsx/modern.xlsx -- the POST-2001 capability fixture.
#
# WHY THIS EXISTS. The Enron corpus (docs/xlsx_design.md §13.I/J) is 2001 and
# cannot contain a single function added since: no SUMIFS, no IFERROR, no
# XLOOKUP, no TEXTJOIN. Everything the corpus proves is about the durable core.
# This fixture covers the other half, and nothing else can.
#
# WHY LIBREOFFICE GENERATES IT, AND THE LIMIT OF THAT. The cached values in an
# xlsx are what make it an oracle -- xlsx.check compares our evaluation against
# them. Hand-writing them, as tools/make_xlsx_fixture.py must for basic.xlsx,
# only ever proves self-consistency: if I misunderstand SUMIFS I will write the
# wrong expected value and the test will agree with me. Having a real
# spreadsheet engine compute them removes exactly that failure.
#
# The honest caveat: LibreOffice is NOT Excel. It is an independent
# implementation of the same specification, so agreement is strong evidence and
# not proof, and the two are known to differ in some edge cases. Ranked by
# strength of evidence:
#
#     Excel-authored (the corpus)  >  LibreOffice-authored (this)  >  hand-written
#
# Where LibreOffice's own answer looked doubtful it was checked against the
# documented definition rather than trusted; YEARFRAC basis 1 is REFUSED by the
# evaluator for that reason instead of being matched to whatever came out.
#
# The .xlsx is COMMITTED, so the test suite never runs LibreOffice -- same rule
# as the python fixture generators. This script is only how it was authored.
#
# Usage: tools/make_xlsx_modern_fixture.sh
set -eu
cd "$(dirname "$0")/.."

command -v libreoffice >/dev/null 2>&1 || { echo "needs libreoffice"; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
csv="$work/modern.csv"

# Tab-separated so the commas inside formulas survive import. Columns:
#   A region   B product   C amount   D date       E..  formulas
# Rows 1-6 are data; the formula block starts at row 1 in columns F/G so that
# every criteria function has a real multi-column table to work over rather
# than the degenerate single-column case.
printf 'region\tproduct\tamount\tdate\tlabel\tformula\n'                              >"$csv"
printf 'East\tLoan A\t1200\t=DATE(2026,1,15)\tSUMIFS region\t=SUMIFS(C2:C7,A2:A7,"East")\n'        >>"$csv"
printf 'West\tLoan B\t800\t=DATE(2026,2,28)\tSUMIFS two crit\t=SUMIFS(C2:C7,A2:A7,"East",B2:B7,"Loan A")\n' >>"$csv"
printf 'East\tLoan B\t1500\t=DATE(2026,3,31)\tCOUNTIFS\t=COUNTIFS(A2:A7,"East",C2:C7,">1000")\n'   >>"$csv"
printf 'West\tLoan A\t400\t=DATE(2026,4,1)\tAVERAGEIFS\t=AVERAGEIFS(C2:C7,A2:A7,"West")\n'         >>"$csv"
printf 'East\tLoan A\t950\t=DATE(2026,6,30)\tMAXIFS\t=MAXIFS(C2:C7,A2:A7,"East")\n'                >>"$csv"
printf 'North\tOther\t2200\t=DATE(2026,12,31)\tMINIFS\t=MINIFS(C2:C7,A2:A7,"East")\n'              >>"$csv"
# Remaining formulas, one per row, in column F with a label in column E.
add() { printf '\t\t\t\t%s\t%s\n' "$1" "$2" >>"$csv"; }
add 'SUMIF'            '=SUMIF(A2:A7,"East",C2:C7)'
add 'SUMIF wildcard'   '=SUMIF(B2:B7,"Loan*",C2:C7)'
add 'COUNTIF ne'       '=COUNTIF(A2:A7,"<>East")'
add 'AVERAGEIF'        '=AVERAGEIF(C2:C7,">=1000")'
add 'IFERROR'          '=IFERROR(1/0,"caught")'
add 'IFNA hit'         '=IFNA(NA(),"was-na")'
add 'IFNA passthru'    '=IFNA(42,"was-na")'
add 'IFS'              '=IFS(C2>2000,"huge",C2>1000,"big",TRUE,"small")'
add 'SWITCH'           '=SWITCH(A3,"East","e","West","w","other")'
add 'SWITCH default'   '=SWITCH("zzz","East","e","fallback")'
add 'XOR'              '=XOR(TRUE,FALSE,TRUE)'
add 'CONCAT'           '=CONCAT(A2:A4)'
add 'CONCATENATE'      '=CONCATENATE(A2,"-",B2)'
add 'TEXTJOIN'         '=TEXTJOIN("|",TRUE,A2:A7)'
add 'TEXTBEFORE'       '=TEXTBEFORE("Loan A - East"," - ")'
add 'TEXTAFTER'        '=TEXTAFTER("Loan A - East"," - ")'
add 'XLOOKUP'          '=XLOOKUP("West",A2:A7,C2:C7)'
add 'XLOOKUP missing'  '=XLOOKUP("South",A2:A7,C2:C7,"none")'
add 'XMATCH'           '=XMATCH("North",A2:A7)'
add 'EOMONTH 0'        '=EOMONTH(D2,0)'
add 'EOMONTH +2'       '=EOMONTH(D2,2)'
add 'EOMONTH -1'       '=EOMONTH(D2,-1)'
add 'EDATE'            '=EDATE(D3,1)'
add 'DAYS'             '=DAYS(D4,D2)'
add 'YEARFRAC us'      '=YEARFRAC(D2,D6,0)'
add 'YEARFRAC act360'  '=YEARFRAC(D2,D6,2)'
add 'YEARFRAC act365'  '=YEARFRAC(D2,D6,3)'
add 'YEARFRAC euro'    '=YEARFRAC(D2,D6,4)'
add 'NETWORKDAYS'      '=NETWORKDAYS(D2,D3)'
add 'ISOWEEKNUM'       '=ISOWEEKNUM(D2)'
add 'WEEKDAY 1'        '=WEEKDAY(D2,1)'
add 'WEEKDAY 2'        '=WEEKDAY(D2,2)'
add 'YEAR'             '=YEAR(D6)'
add 'MONTH'            '=MONTH(D6)'
add 'DAY'              '=DAY(D6)'
add 'DATE normalise'   '=DATE(2026,13,1)'
add 'STDEV.S'          '=STDEV.S(C2:C7)'
add 'STDEV.P'          '=STDEV.P(C2:C7)'
add 'VAR.S'            '=VAR.S(C2:C7)'
add 'VAR.P'            '=VAR.P(C2:C7)'
add 'MEDIAN'           '=MEDIAN(C2:C7)'
add 'PERCENTILE.INC'   '=PERCENTILE.INC(C2:C7,0.25)'
add 'QUARTILE.INC'     '=QUARTILE.INC(C2:C7,3)'
add 'RANK.EQ desc'     '=RANK.EQ(1500,C2:C7)'
add 'RANK.EQ asc'      '=RANK.EQ(1500,C2:C7,1)'
add 'SMALL'            '=SMALL(C2:C7,2)'
add 'LARGE'            '=LARGE(C2:C7,2)'
add 'AGGREGATE sum'    '=AGGREGATE(9,0,C2:C7)'
add 'AGGREGATE skiperr' '=AGGREGATE(9,6,C2:C7)'
add 'SUBTOTAL 109'     '=SUBTOTAL(109,C2:C7)'

libreoffice --headless --infilter="CSV:9,34,76,1" --convert-to xlsx "$csv" \
    --outdir "$work" >/dev/null 2>&1

out=examples/fixtures/xlsx/modern.xlsx
mkdir -p "$(dirname "$out")"
cp "$work/modern.xlsx" "$out"
printf 'wrote %s (%s bytes)\n' "$out" "$(stat -c%s "$out")"
