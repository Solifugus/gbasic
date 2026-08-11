#!/usr/bin/env bash
# Build examples/fixtures/xlsx/crosssheet.xlsx -- the cross-sheet fixture.
#
# WHY. Measured over the Enron corpus, references to another sheet were the
# single largest cause of disagreement: 3.09M cells, 54% of them
# (docs/xlsx_design.md §13.J). The three populations are 42% quoted sheet names
# ('Nymex hist.'!A:B), 30% plain (Data!$A$1), 28% EXTERNAL ([4]CurveFetch!$D$8),
# and the overwhelming consumer is VLOOKUP.
#
# HOW THE CACHED VALUES ARE OBTAINED, and why it is done this way. The sheet
# XML is hand-written so the exact shapes under test are guaranteed present --
# a quoted name, a whole-column range, an approximate VLOOKUP -- but with NO
# <v> elements at all. LibreOffice is then asked to convert the file, and
# because the cached values are missing it must compute them. So the shapes are
# ours and the ANSWERS are an independent implementation's.
#
# That is the same oracle argument as modern.xlsx, and the same caveat applies:
# LibreOffice is not Excel, so agreement is strong evidence, not proof. The
# numbers here are small enough to check by eye as a third leg.
#
# The .xlsx is committed; the suite never runs LibreOffice.
set -eu
cd "$(dirname "$0")/.."
command -v libreoffice >/dev/null 2>&1 || { echo "needs libreoffice"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "needs python3 to author the XML"; exit 1; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 - "$work" <<'PY'
import sys, zipfile, os
work = sys.argv[1]
def esc(s): return s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;")

# Sheet 1 name contains a SPACE, which forces every reference to it into the
# quoted form -- the largest of the three corpus populations.
tbl = "".join(
    f'<row r="{i}"><c r="A{i}" t="inlineStr"><is><t>{nm}</t></is></c>'
    f'<c r="B{i}"><v>{v}</v></c></row>'
    for i, (nm, v) in enumerate([("East",10),("North",20),("South",30),("West",40)], 1))

formulas = [
    "'Rate Table'!B2",                                     # bare cross-sheet cell
    "SUM('Rate Table'!B1:B4)",                             # cross-sheet range
    "VLOOKUP(A1,'Rate Table'!A1:B4,2,0)",                  # exact, quoted sheet
    "VLOOKUP(\"North\",'Rate Table'!A:B,2,0)",             # WHOLE COLUMN range
    "VLOOKUP(25,'Rate Table'!B1:B4,1)",                    # APPROXIMATE (no 4th arg)
    "HLOOKUP(\"b\",'Rate Table'!A6:C7,2,0)",               # horizontal
    "MATCH(\"West\",'Rate Table'!A1:A4,0)",
    "INDEX('Rate Table'!B1:B4,3)",
    "INDEX('Rate Table'!A1:B4,2,2)",                       # two-dimensional
    "COUNTA('Rate Table'!A:A)",                            # whole column, count
    "AVERAGE('Rate Table'!B:B)",
    "SUMIF('Rate Table'!A1:A4,\"South\",'Rate Table'!B1:B4)",
    "IF(ISNA(VLOOKUP(\"Nowhere\",'Rate Table'!A1:B4,2,0)),\"missing\",\"found\")",
]
# A tiny horizontal table for HLOOKUP, on the same sheet.
tbl += ('<row r="6"><c r="A6" t="inlineStr"><is><t>a</t></is></c>'
        '<c r="B6" t="inlineStr"><is><t>b</t></is></c>'
        '<c r="C6" t="inlineStr"><is><t>c</t></is></c></row>'
        '<row r="7"><c r="A7"><v>1</v></c><c r="B7"><v>2</v></c>'
        '<c r="C7"><v>3</v></c></row>')

rows = ['<row r="1"><c r="A1" t="inlineStr"><is><t>South</t></is></c></row>']
for i, f in enumerate(formulas, start=2):
    rows.append(f'<row r="{i}"><c r="A{i}"><f>{esc(f)}</f></c></row>')   # no <v>

def sheet(body):
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f'<sheetData>{body}</sheetData></worksheet>')

parts = {
 '[Content_Types].xml':'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>',
 '_rels/.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
 'xl/workbook.xml':'<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Rate Table" sheetId="1" r:id="rId1"/><sheet name="Calc" sheetId="2" r:id="rId2"/></sheets></workbook>',
 'xl/_rels/workbook.xml.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/></Relationships>',
 'xl/worksheets/sheet1.xml':sheet(tbl),
 'xl/worksheets/sheet2.xml':sheet("".join(rows)),
}
with zipfile.ZipFile(os.path.join(work,'in.xlsx'),'w',zipfile.ZIP_DEFLATED) as z:
    for k,v in parts.items(): z.writestr(k,v)
PY

libreoffice --headless --convert-to xlsx "$work/in.xlsx" --outdir "$work/conv" >/dev/null 2>&1

out=examples/fixtures/xlsx/crosssheet.xlsx
mkdir -p "$(dirname "$out")"
cp "$work/conv/in.xlsx" "$out"
printf 'wrote %s (%s bytes)\n' "$out" "$(stat -c%s "$out")"
