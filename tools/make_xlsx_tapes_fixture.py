#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/tapes.xlsx -- the L3 consolidation fixture.

The participation-loan / CECL problem in miniature: several sheets carrying the
SAME MEANING with a different SURFACE. Every difference here is one
docs/xlsx_design.md §6 names.

  CU_A   "Loan #"       "Balance"    "Int Rate"        "Orig Date"
         balance a bare number, rate a WHOLE percent (5.25 meaning 5.25%),
         date an Excel serial

  CU_B   "Note ID"      "Principal"  "Interest Rate"   "Origination"
         balance TEXT with a currency symbol and thousands separators,
         rate a FRACTION (0.0475 meaning 4.75%) -- the same column meaning the
         same thing on a different scale, which is the trap: both are plausible
         numbers and nothing in the file says which is which

  CU_C   "loan_number"  "Amount"     "Rate (%)"        "Date"
         balance TEXT with a PARENTHESISED NEGATIVE, rate a string "6.0 %",
         date a string, and the header names differ only by punctuation and
         case from the canonical ones -- the case normalised matching should
         get without an alias

  CU_D   "Loan #"       "Int Rate"   -- and NO balance column at all.
         A source missing a REQUIRED column must be reported, not quietly
         emitted with unknowns, because a tape silently short a balance column
         understates the pool.

Written by hand: there are no formulas, so there is nothing for another engine
to compute -- the surface variation IS the content.
"""
import zipfile, os

def txt(ref, v, s=None):
    sa = f' s="{s}"' if s else ''
    v = v.replace("&", "&amp;").replace("<", "&lt;")
    return f'<c r="{ref}" t="inlineStr"{sa}><is><t>{v}</t></is></c>'

def num(ref, v, s=None):
    sa = f' s="{s}"' if s else ''
    return f'<c r="{ref}"{sa}><v>{v}</v></c>'

def sheet_xml(rows):
    body = "".join(f'<row r="{i}">' + "".join(cs) + "</row>" for i, cs in rows)
    return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
            '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
            f'<sheetData>{body}</sheetData></worksheet>')

sheets = {}

# --- CU_A: numbers throughout, rate as a WHOLE percent -----------------------
rows = [(1, [txt("A1","Loan #"), txt("B1","Balance"), txt("C1","Int Rate"), txt("D1","Orig Date")])]
for i,(lid,bal,rate,dt) in enumerate([("A-100",150000,5.25,45000),
                                      ("A-101",82500.5,4.875,45031)], start=2):
    rows.append((i,[txt(f"A{i}",lid), num(f"B{i}",bal,1), num(f"C{i}",rate), num(f"D{i}",dt,2)]))
sheets["CU_A"] = sheet_xml(rows)

# --- CU_B: money as text, rate as a FRACTION ---------------------------------
rows = [(1, [txt("A1","Note ID"), txt("B1","Principal"), txt("C1","Interest Rate"), txt("D1","Origination")])]
for i,(lid,bal,rate,dt) in enumerate([("B-200","$1,500.00",0.0475,45100),
                                      ("B-201","$23,750.25",0.0525,45140)], start=2):
    rows.append((i,[txt(f"A{i}",lid), txt(f"B{i}",bal), num(f"C{i}",rate), num(f"D{i}",dt,2)]))
sheets["CU_B"] = sheet_xml(rows)

# --- CU_C: parenthesised negative, rate as a string with a % sign ------------
rows = [(1, [txt("A1","loan_number"), txt("B1","Amount"), txt("C1","Rate (%)"), txt("D1","Date")])]
for i,(lid,bal,rate,dt) in enumerate([("C-300","(1,200.00)","6.0 %","2023-05-14"),
                                      ("C-301","9,000","6.5%","2023-06-01")], start=2):
    rows.append((i,[txt(f"A{i}",lid), txt(f"B{i}",bal), txt(f"C{i}",rate), txt(f"D{i}",dt)]))
sheets["CU_C"] = sheet_xml(rows)

# --- CU_D: missing a REQUIRED column -----------------------------------------
rows = [(1, [txt("A1","Loan #"), txt("C1","Int Rate")])]
for i,(lid,rate) in enumerate([("D-400",7.0)], start=2):
    rows.append((i,[txt(f"A{i}",lid), num(f"C{i}",rate)]))
sheets["CU_D"] = sheet_xml(rows)

names = list(sheets)
ov = "".join(f'<Override PartName="/xl/worksheets/sheet{i}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>' for i in range(1,len(names)+1))
sl = "".join(f'<sheet name="{n}" sheetId="{i}" r:id="rId{i}"/>' for i,n in enumerate(names,1))
rl = "".join(f'<Relationship Id="rId{i}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(names)+1))
rl += f'<Relationship Id="rIdS" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>'

parts = {
 '[Content_Types].xml':'<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'+ov+'<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>',
 '_rels/.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
 'xl/workbook.xml':'<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'+sl+'</sheets></workbook>',
 'xl/_rels/workbook.xml.rels':'<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+rl+'</Relationships>',
 'xl/styles.xml':'<?xml version="1.0"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="1"><numFmt numFmtId="164" formatCode="&quot;$&quot;#,##0.00"/></numFmts><cellXfs count="3"><xf numFmtId="0" xfId="0"/><xf numFmtId="164" xfId="0" applyNumberFormat="1"/><xf numFmtId="14" xfId="0" applyNumberFormat="1"/></cellXfs></styleSheet>',
}
for i,n in enumerate(names,1):
    parts[f'xl/worksheets/sheet{i}.xml'] = sheets[n]

out = os.path.join(os.path.dirname(__file__), '..', 'examples', 'fixtures', 'xlsx', 'tapes.xlsx')
out = os.path.normpath(out)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, b in parts.items():
        z.writestr(name, b)
print("wrote", out, os.path.getsize(out), "bytes,", len(names), "tapes")
