#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/south.xlsx, the shape of the gBASIC books'
"South" service sheet (volume 3, Chapter 6), for grid's gaps G19, G20, G22.

  row 1  Visit (merged A1:C1)  Labour (merged D1:E1)  Parts (F1, no merge)  -
  row 2  Serial Date Tech      Hours Rate             Cost                  Warranty Hours
  rows 3-  visits, a blank row, two "<name> subtotal" rows, and a grand total

What it pins: G's "Warranty" has no parent (Parts spans F alone, so carrying
across blanks named it "Parts Warranty"); the second "Hours" in H is a second
column, not merged into D's; and "Garza subtotal" is a total. Committed with
basic.xlsx's rule: python3 is not in the test path.
"""
import os
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples", "fixtures", "xlsx")


def cell(ref, v):
    if isinstance(v, str):
        return '<c r="%s" t="inlineStr"><is><t>%s</t></is></c>' % (ref, v)
    return '<c r="%s"><v>%r</v></c>' % (ref, v)


rows = [
    (1, ["Visit", None, None, "Labour", None, "Parts", None, None]),
    (2, ["Serial", "Date", "Tech", "Hours", "Rate", "Cost", "Warranty", "Hours"]),
    (3, ["S-101", "2026-11-02", "Garza", 2, 90, 40, "yes", 0.5]),
    (4, ["S-102", "2026-11-03", "Garza", 1.5, 90, 0, "no", 0]),
    (5, ["Garza subtotal", None, None, 3.5, None, 40, None, None]),
    (6, [None] * 8),
    (7, ["S-103", "2026-11-04", "Okafor", 3, 95, 120, "yes", 1]),
    (8, ["Okafor subtotal", None, None, 3, None, 120, None, None]),
    (9, ["Total", None, None, 6.5, None, 160, None, None]),
]
cols = "ABCDEFGH"
data = []
for r, vals in rows:
    cs = "".join(cell("%s%d" % (cols[i], r), v) for i, v in enumerate(vals) if v is not None)
    data.append('<row r="%d">%s</row>' % (r, cs) if cs else '<row r="%d"/>' % r)
sheet = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n<worksheet xmlns="http://schemas.openxmlformats.org/'
         'spreadsheetml/2006/main"><dimension ref="A1:H9"/><sheetData>%s</sheetData><mergeCells count="2">'
         '<mergeCell ref="A1:C1"/><mergeCell ref="D1:E1"/></mergeCells></worksheet>' % "".join(data))
parts = {
    "[Content_Types].xml": '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>',
    "_rels/.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
    "xl/workbook.xml": '<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="South" sheetId="1" r:id="rId1"/></sheets></workbook>',
    "xl/_rels/workbook.xml.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>',
    "xl/worksheets/sheet1.xml": sheet,
}
with zipfile.ZipFile(os.path.join(OUT, "south.xlsx"), "w", zipfile.ZIP_DEFLATED) as z:
    for k, v in parts.items():
        z.writestr(zipfile.ZipInfo(k, date_time=(2026, 11, 5, 0, 0, 0)), v, zipfile.ZIP_DEFLATED)
