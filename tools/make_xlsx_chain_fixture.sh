#!/usr/bin/env bash
# Build examples/fixtures/xlsx/chain.xlsx -- the cross-sheet DEPENDENCY CHAIN.
#
# WHY. xlsx.recalc(wb, sheet) orders one sheet. Once cross-sheet references
# worked that became a trap: a formula on one sheet can depend on a FORMULA on
# another, and recalculating only the first reads the second's stale cached
# value -- a confident wrong number, which is the failure this module exists to
# avoid. This fixture is the smallest thing that demonstrates it.
#
# THE CHAIN, and the ordering trap built into it:
#
#     Inputs!A1 = 10                 a literal
#     Mid!A1    = Inputs!A1 * 2      cached 20
#     Out!A1    = Mid!A1 + 1         cached 21
#
# SHEET ORDER IS DELIBERATELY Out, Mid, Inputs -- the reverse of the dependency
# order. An engine that recalculated sheets in workbook order would hand Out a
# stale Mid and print a plausible wrong number, exactly as D7/B5 does within a
# single sheet in basic.xlsx. Set Inputs!A1 to 100 and the only correct answers
# are Mid=200 and Out=201; a stale read gives Out=21.
#
# Out!A2 also holds a two-sheet SUM so the test covers a range, and Cycle!A1/B1
# are a cross-sheet circular reference (A1 -> Cycle2!B1 -> A1), which must be
# REPORTED rather than iterated -- with a healthy cell beside it that must still
# evaluate, since one cycle must not sink the workbook.
#
# Cached values come from LibreOffice: the XML is hand-written with no <v>, so
# it must compute them to convert the file. The .xlsx is committed.
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
def sh(body):
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f'<sheetData>{body}</sheetData></worksheet>')

# Sheets are declared Out, Mid, Inputs -- the REVERSE of dependency order.
sheets = [
    ("Out",    '<row r="1"><c r="A1"><f>Mid!A1+1</f></c>'
               '<c r="B1"><f>SUM(Mid!A1:A2)+Inputs!A1</f></c></row>'),
    ("Mid",    '<row r="1"><c r="A1"><f>Inputs!A1*2</f></c></row>'
               '<row r="2"><c r="A2"><v>5</v></c></row>'),
    ("Inputs", '<row r="1"><c r="A1"><v>10</v></c></row>'),
    # A cycle that spans two sheets, plus a healthy neighbour.
    ("Cycle",  '<row r="1"><c r="A1"><f>Cycle2!B1+1</f></c>'
               '<c r="B1"><f>Inputs!A1*3</f></c></row>'),
    ("Cycle2", '<row r="1"><c r="B1"><f>Cycle!A1+1</f></c></row>'),
]

ov = "".join(
    f'<Override PartName="/xl/worksheets/sheet{i}.xml" '
    'ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
    for i in range(1, len(sheets) + 1))
sl = "".join(f'<sheet name="{n}" sheetId="{i}" r:id="rId{i}"/>'
             for i, (n, _) in enumerate(sheets, 1))
rl = "".join(f'<Relationship Id="rId{i}" '
             'Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" '
             f'Target="worksheets/sheet{i}.xml"/>' for i in range(1, len(sheets) + 1))

parts = {
 '[Content_Types].xml':'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+ov+'</Types>',
 '_rels/.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
 'xl/workbook.xml':'<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'+sl+'</sheets></workbook>',
 'xl/_rels/workbook.xml.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+rl+'</Relationships>',
}
for i, (n, body) in enumerate(sheets, 1):
    parts[f'xl/worksheets/sheet{i}.xml'] = sh(body)

with zipfile.ZipFile(os.path.join(work,'in.xlsx'),'w',zipfile.ZIP_DEFLATED) as z:
    for k,v in parts.items(): z.writestr(k,v)
PY

libreoffice --headless --convert-to xlsx "$work/in.xlsx" --outdir "$work/conv" >/dev/null 2>&1
out=examples/fixtures/xlsx/chain.xlsx
cp "$work/conv/in.xlsx" "$out"
printf 'wrote %s (%s bytes)\n' "$out" "$(stat -c%s "$out")"
