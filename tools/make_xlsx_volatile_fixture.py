#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/volatile.xlsx -- the NOW/TODAY fixture.

A SEPARATE fixture, for the same reason macro_sheet.xlsx is one: basic.xlsx
pins the read/write/retention goldens byte-exactly, including its container
entry count, so adding a sheet there would rebaseline all of them for an
unrelated concern.

The cached values below are deliberately STALE -- they are the serial for
2025-08-01, which is not when any test runs. That is the point: a volatile
cell's cache dates from whenever the workbook last calculated, so `xlsx.check`
must report these as skipped rather than comparing against them. A fixture whose
cached values happened to be right would let a broken skip pass.
"""
import zipfile, os

# 2025-08-01 12:00 -- stale on purpose (see above).
STALE_NOW = 45870.5
STALE_TODAY = 45870

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
parts['xl/workbook.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
    'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'
    '<sheet name="Volatile" sheetId="1" r:id="rId1"/>'
    '</sheets></workbook>')
parts['xl/_rels/workbook.xml.rels'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
    '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
    '</Relationships>')

# A1 TODAY, A2 NOW, plus B1 -- a NON-volatile formula on the same sheet, so the
# tests can show that one volatile cell does not stop the rest being judged.
parts['xl/worksheets/sheet1.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
    f'<row r="1"><c r="A1"><f>TODAY()</f><v>{STALE_TODAY}</v></c>'
    '<c r="B1"><f>2+3</f><v>5</v></c></row>'
    f'<row r="2"><c r="A2"><f>NOW()</f><v>{STALE_NOW}</v></c></row>'
    '</sheetData></worksheet>')

out = os.path.join(os.path.dirname(__file__), '..', 'examples', 'fixtures', 'xlsx', 'volatile.xlsx')
out = os.path.normpath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, body in parts.items():
        z.writestr(name, body)
print("wrote", out, os.path.getsize(out), "bytes,", len(parts), "parts")
