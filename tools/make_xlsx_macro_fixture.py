#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/macro_sheet.xlsx -- the partless-sheet fixture.

WHY IT IS A SEPARATE FIXTURE. basic.xlsx pins the read/write/retention goldens
byte-exactly; adding a sheet to it would rebaseline all of them for a concern
that has nothing to do with those tests. This one file isolates a single shape.

WHAT IT REPRODUCES, and where the shape came from. Excel writes VBA module and
macro sheets into workbook.xml with an EMPTY relationship id:

    <sheet name="Module1" state="veryHidden" r:id=""/>

The sheet is genuinely in the workbook; there is simply no worksheet part behind
it. Our reader used to list such a sheet from xlsx.sheets and then raise
"no such sheet" from xlsx.cells -- an internal contradiction that broke the one
loop every caller writes:

    for each s in xlsx.sheets(wb) / for each c in xlsx.cells(wb, s)

Found by scanning the 15,871-workbook Enron corpus (figshare 10.6084/m9.figshare
.1221767, CC BY 4.0): 400 workbooks (2.5%) contain such a sheet, and every one
of the 400 scan failures was this single case -- there was no ZIP, XML or
cell-parsing failure anywhere in the corpus.

Both observed spellings are included:
  * "Module1"  -- the ordinary case
  * "VBACode " -- WITH A TRAILING SPACE, exactly as found in seven of the
                  corpus workbooks. A reader that trims sheet names would look
                  correct on Module1 and still fail on this one.
"""
import zipfile, os

parts = {}
parts['[Content_Types].xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
    '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
    '<Default Extension="xml" ContentType="application/xml"/>'
    '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
    '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
    '</Types>')
parts['_rels/.rels'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>'
    '</Relationships>')

# Sheet order deliberately puts a partless sheet BETWEEN two real ones, so an
# iteration that aborted on it would also lose the sheet after it.
parts['xl/workbook.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
    'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'
    '<sheet name="Data" sheetId="1" r:id="rId1"/>'
    '<sheet name="Module1" sheetId="2" state="veryHidden" r:id=""/>'
    '<sheet name="VBACode " sheetId="3" state="veryHidden" r:id=""/>'
    '<sheet name="After" sheetId="4" r:id="rId2"/>'
    '</sheets></workbook>')
parts['xl/_rels/workbook.xml.rels'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
    '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>'
    '</Relationships>')
parts['xl/worksheets/sheet1.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
    '<row r="1"><c r="A1"><v>3</v></c><c r="A2"><v>4</v></c></row>'
    '<row r="2"><c r="B1"><f>A1+A2</f><v>7</v></c></row>'
    '</sheetData></worksheet>')
parts['xl/worksheets/sheet2.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
    '<row r="1"><c r="A1" t="inlineStr"><is><t>reached</t></is></c></row>'
    '</sheetData></worksheet>')

out = os.path.join(os.path.dirname(__file__), '..', 'examples', 'fixtures', 'xlsx', 'macro_sheet.xlsx')
out = os.path.normpath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, body in parts.items():
        z.writestr(name, body)
print("wrote", out, os.path.getsize(out), "bytes,", len(parts), "parts")
