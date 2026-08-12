#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/messy.xlsx -- the L2 extraction fixture.

Every irregularity here is one named in docs/xlsx_design.md §5 as the reason
that layer exists. None of it is decoration:

  * a TITLE and a run date above the table, so row 1 is not the header
  * a BLANK ROW between title and table
  * a TWO-ROW HEADER where the top row is a merged-style parent that only
    appears over the first of its children ("Q1" above "Units"/"Value")
  * SUBTOTAL rows interleaved with data, which a naive reader adds to the data
  * a GRAND TOTAL row
  * a TRAILING NOTE after a blank row, which a "read to the bottom" reader
    swallows as data
  * a SECOND TABLE further down with a DIFFERENT column count and its own
    header, so a single-table detector reports the wrong shape
  * a column of dates stored as serials, and money stored as bare numbers --
    the type information lives only in the style, exactly as §13.B says
  * a genuinely EMPTY column (D) inside the first table's span, because a real
    sheet has spacer columns

The whole point of the fixture is that the CORRECT answer is not what a naive
top-left-to-bottom-right reader produces, so a test over it fails loudly rather
than looking plausible.

Hand-written rather than LibreOffice-generated: there are no formulas here, so
there is nothing for another engine to compute -- the structure IS the content,
and writing it directly is what guarantees the awkward shapes are present.
"""
import zipfile, os

def c(ref, v, t=None, s=None):
    ta = f' t="{t}"' if t else ''
    sa = f' s="{s}"' if s else ''
    if t == "inlineStr":
        return f'<c r="{ref}"{ta}{sa}><is><t>{v}</t></is></c>'
    return f'<c r="{ref}"{ta}{sa}><v>{v}</v></c>'

def txt(ref, v, s=None): return c(ref, v, "inlineStr", s)
def num(ref, v, s=None): return c(ref, v, None, s)

rows = {}
def put(r, cells): rows.setdefault(r, []).extend(cells)

# --- Sheet "Report": the messy one -------------------------------------------
put(1, [txt("A1", "ACME HOLDINGS — QUARTERLY UNIT REPORT")])
put(2, [txt("A2", "Run date:"), num("B2", 46023, s=2)])     # a date serial
# row 3 blank
# Two-row header: the parent appears only above the first of its children.
put(4, [txt("B4", "Q1"), txt("E4", "Q2")])
put(5, [txt("A5", "Region"), txt("B5", "Units"), txt("C5", "Value"),
        txt("E5", "Units"), txt("F5", "Value")])
# Data, with subtotals interleaved. Column D is deliberately empty throughout.
data = [
    ("East",   120, 2400.50,  130, 2600.00),
    ("West",    90, 1800.00,   95, 1900.25),
]
r = 6
for name, u1, v1, u2, v2 in data:
    put(r, [txt(f"A{r}", name), num(f"B{r}", u1), num(f"C{r}", v1, s=1),
            num(f"E{r}", u2), num(f"F{r}", v2, s=1)])
    r += 1
put(r, [txt(f"A{r}", "Subtotal North"), num(f"B{r}", 210), num(f"C{r}", 4200.50, s=1),
        num(f"E{r}", 225), num(f"F{r}", 4500.25, s=1)])
r += 1
data2 = [
    ("South",   70, 1400.00,   75, 1500.00),
    ("Central", 60, 1200.75,   65, 1300.50),
]
for name, u1, v1, u2, v2 in data2:
    put(r, [txt(f"A{r}", name), num(f"B{r}", u1), num(f"C{r}", v1, s=1),
            num(f"E{r}", u2), num(f"F{r}", v2, s=1)])
    r += 1
put(r, [txt(f"A{r}", "TOTAL"), num(f"B{r}", 340), num(f"C{r}", 6801.25, s=1),
        num(f"E{r}", 365), num(f"F{r}", 7300.75, s=1)])
total_row = r
r += 2                                    # a blank row, then a note
put(r, [txt(f"A{r}", "Note: Q2 figures are provisional.")])
note_row = r

# A SECOND table lower down, different width, its own header.
second_start = note_row + 2
put(second_start, [txt(f"A{second_start}", "Adjustments")])
put(second_start + 1, [txt(f"A{second_start+1}", "Ref"), txt(f"B{second_start+1}", "Amount")])
adj = [("ADJ-1", -125.00), ("ADJ-2", 40.25), ("ADJ-3", -12.50)]
for i, (ref, amt) in enumerate(adj):
    rr = second_start + 2 + i
    put(rr, [txt(f"A{rr}", ref), num(f"B{rr}", amt, s=1)])

body = "".join(
    f'<row r="{k}">' + "".join(v) + "</row>" for k, v in sorted(rows.items()))

# A clean second sheet, for the "automatic when safe" path.
clean_rows = ['<row r="1">' + txt("A1","Item") + txt("B1","Qty") + txt("C1","Price") + '</row>']
for i, (nm, q, p) in enumerate([("bolt",10,0.25),("nut",20,0.1),("washer",5,0.05)], start=2):
    clean_rows.append(f'<row r="{i}">' + txt(f"A{i}",nm) + num(f"B{i}",q) + num(f"C{i}",p,s=1) + '</row>')
clean = "".join(clean_rows)

def sheet(b):
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f'<sheetData>{b}</sheetData></worksheet>')

parts = {
 '[Content_Types].xml':'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>',
 '_rels/.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
 'xl/workbook.xml':'<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Report" sheetId="1" r:id="rId1"/><sheet name="Clean" sheetId="2" r:id="rId2"/></sheets></workbook>',
 'xl/_rels/workbook.xml.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>',
 # s=1 money, s=2 date -- the only thing distinguishing 46023 from a number.
 'xl/styles.xml':'<?xml version="1.0"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="1"><numFmt numFmtId="164" formatCode="&quot;$&quot;#,##0.00"/></numFmts><cellXfs count="3"><xf numFmtId="0" xfId="0"/><xf numFmtId="164" xfId="0" applyNumberFormat="1"/><xf numFmtId="14" xfId="0" applyNumberFormat="1"/></cellXfs></styleSheet>',
 'xl/worksheets/sheet1.xml':sheet(body),
 'xl/worksheets/sheet2.xml':sheet(clean),
}

out = os.path.join(os.path.dirname(__file__), '..', 'examples', 'fixtures', 'xlsx', 'messy.xlsx')
out = os.path.normpath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, b in parts.items():
        z.writestr(name, b)
print("wrote", out, os.path.getsize(out), "bytes")
print("  Report: title/blank/2-row header/data+subtotals/TOTAL/blank/note/2nd table")
print("  total row =", total_row, " note row =", note_row, " 2nd table at", second_start)
