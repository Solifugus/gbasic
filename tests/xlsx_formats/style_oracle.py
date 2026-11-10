#!/usr/bin/env python3
"""What LibreOffice must show of tests/xlsx_formats/style_test.bas's workbook
(gap G55). Every expectation is what that test told the writer; nothing here
is read from gBASIC's own output except the file under test itself, and that
only with Python's own XML parser (never libxml2, which wrote it).

  style_oracle.py MADE.xlsx MADE.fods LO.xlsx

MADE.fods is LibreOffice's flat ODS of MADE.xlsx: each cell's bold, fill and
shown text. LO.xlsx is LibreOffice's own xlsx of it, written from the model
it loaded: the panes, widths and filter it read (a headless .fods has no view
settings, so the freeze is only visible there).
"""
import re
import sys
import zipfile
import xml.etree.ElementTree as ET

made, fods, lo = sys.argv[1:4]
bad = []
n = 0


def want(label, got, expected):
    global n
    n += 1
    if got != expected:
        bad.append(f"{label}: got {got!r}, want {expected!r}")


NS = {
    "office": "urn:oasis:names:tc:opendocument:xmlns:office:1.0",
    "style": "urn:oasis:names:tc:opendocument:xmlns:style:1.0",
    "table": "urn:oasis:names:tc:opendocument:xmlns:table:1.0",
    "text": "urn:oasis:names:tc:opendocument:xmlns:text:1.0",
    "fo": "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0",
}
T = "{%s}" % NS["table"]
doc = ET.parse(fods).getroot()

styles = {}
for st in doc.iter("{%s}style" % NS["style"]):
    name = st.get("{%s}name" % NS["style"])
    tp = st.find("style:text-properties", NS)
    cp = st.find("style:table-cell-properties", NS)
    styles[name] = {
        "bold": tp is not None and tp.get("{%s}font-weight" % NS["fo"]) == "bold",
        "fill": (cp.get("{%s}background-color" % NS["fo"]) if cp is not None else None),
    }


def grid(sheet_name):
    """ref -> (style, shown text), a cell's style falling back to its column's"""
    for tb in doc.iter(T + "table"):
        if tb.get(T + "name") != sheet_name:
            continue
        cols = []
        for c in tb.iter(T + "table-column"):
            cols += [c.get(T + "default-cell-style-name")] * int(c.get(T + "number-columns-repeated", "1"))
        out = {}
        r = 0
        for row in tb.iter(T + "table-row"):
            rr = int(row.get(T + "number-rows-repeated", "1"))
            r += 1
            col = 0
            for cell in row:
                if cell.tag not in (T + "table-cell", T + "covered-table-cell"):
                    continue
                rep = int(cell.get(T + "number-columns-repeated", "1"))
                if rep < 100:
                    for k in range(rep):
                        letters = ""
                        c = col + k + 1
                        while c:
                            c, m = divmod(c - 1, 26)
                            letters = chr(65 + m) + letters
                        st = cell.get(T + "style-name") or (cols[col + k] if col + k < len(cols) else None)
                        shown = "".join("".join(p.itertext()) for p in cell.findall("text:p", NS))
                        out[f"{letters}{r}"] = (styles.get(st, {"bold": False, "fill": None}), shown)
                col += rep
            r += rr - 1
        return out
    return {}


g = grid("Q'3 Report")
for ref in ("A1", "B1", "C1"):
    want(f"{ref} bold", g[ref][0]["bold"], True)
    want(f"{ref} fill", g[ref][0]["fill"], "#d9d9d9")
want("A1 shown", g["A1"][1], "Name")
want("A2 not bold", g["A2"][0]["bold"], False)
want("A2 no fill", g["A2"][0]["fill"] in (None, "transparent"), True)
want("B2 shown as money", g["B2"][1], "1,250.50 USD")
want("B2 not bold", g["B2"][0]["bold"], False)
want("B3 bold, set before its value", g["B3"][0]["bold"], True)
want("B3 shown as money", g["B3"][1], "300.25 USD")
want("C2 bold", g["C2"][0]["bold"], True)
want("C2 kept its percent", g["C2"][1], "50.0%")
want("C3 fill taken off", g["C3"][0]["fill"] in (None, "transparent"), True)
want("A5 bold", g["A5"][0]["bold"], True)
want("B5 the subtotal, bold", g["B5"][0]["bold"], True)
want("B5 the subtotal, as money", g["B5"][1], "1,625.75 USD")
want("D10 bold", g["D10"][0]["bold"], True)

# LibreOffice's own xlsx: what it read of the sheets' views, widths, filter
M = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
with zipfile.ZipFile(lo) as z:
    wbx = ET.fromstring(z.read("xl/workbook.xml"))
    rels = ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))
    target = {r.get("Id"): r.get("Target") for r in rels}
    parts = {}
    for sh in wbx.iter(M + "sheet"):
        rid = sh.get("{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id")
        parts[sh.get("name")] = ET.fromstring(z.read("xl/" + target[rid].lstrip("/").replace("xl/", "")))
rep = parts["Q'3 Report"]
pane = rep.find(f"{M}sheetViews/{M}sheetView/{M}pane")
want("a pane", pane is not None, True)
if pane is not None:
    want("  frozen", pane.get("state"), "frozen")
    want("  one row", pane.get("ySplit"), "1")
    want("  no column (the second freeze replaced the first)", pane.get("xSplit", "0"), "0")
    want("  from A2", pane.get("topLeftCell"), "A2")
widths = [(c.get("min"), c.get("max"), c.get("width")) for c in rep.iter(M + "col") if c.get("customWidth") in ("true", "1")]
want("widths, replacing [30]", widths, [("1", "1", "18"), ("2", "2", "14"), ("3", "3", "10")])
af = rep.find(M + "autoFilter")
want("a filter over A1:C4, replacing A1:C3", af.get("ref") if af is not None else None, "A1:C4")
emp = parts["Empty"]
want("Empty: no frozen pane", [p.get("state") for p in emp.iter(M + "pane") if p.get("state") == "frozen"], [])
want("Empty: no widths", [c for c in emp.iter(M + "col") if c.get("customWidth") in ("true", "1")], [])
want("Empty: no filter", emp.find(M + "autoFilter"), None)

# the file itself: one style for fifty bold cells, one filter name
with zipfile.ZipFile(made) as z:
    st = ET.fromstring(z.read("xl/styles.xml"))
    wb = ET.fromstring(z.read("xl/workbook.xml"))
xfs = st.find(M + "cellXfs")
want("cellXfs count is its children", int(xfs.get("count")), len(xfs))
want("fonts: the default and its bold", len(st.find(M + "fonts")), 2)
want("fills: none, gray125, the grey, the yellow", len(st.find(M + "fills")), 4)
# default; heading; money; bold money; percent; bold percent; the yellow
# fill (C3, taken off again, when it is the default once more); bold General
# (A5's text and the fifty numbers alike) -- each reused after it is made
want("cell styles, each made once", len(xfs), 8)
names = [(d.get("name"), d.get("localSheetId"), d.text) for d in wb.iter(M + "definedName")]
want("the filter's hidden name", names, [("_xlnm._FilterDatabase", "0", "'Q''3 Report'!$A$1:$C$4")])

# Excel refuses a worksheet whose children are out of the schema's order
# (CT_Worksheet, ECMA-376 Part 1 §18.3.1.99); LibreOffice does not, so its
# opening the file says nothing of this
ORDER = ("sheetPr dimension sheetViews sheetFormatPr cols sheetData sheetCalcPr sheetProtection protectedRanges "
         "scenarios autoFilter sortState dataConsolidate customSheetViews mergeCells phoneticPr "
         "conditionalFormatting dataValidations hyperlinks printOptions pageMargins pageSetup headerFooter").split()
with zipfile.ZipFile(made) as z:
    for name in sorted(x for x in z.namelist() if x.startswith("xl/worksheets/")):
        kids = [c.tag.replace(M, "") for c in ET.fromstring(z.read(name))]
        want(f"{name}: children in the schema's order", kids, sorted(kids, key=ORDER.index))
    rep_kids = [c.tag.replace(M, "") for c in ET.fromstring(z.read("xl/worksheets/sheet1.xml"))]
want("the report sheet holds each once", rep_kids, ["sheetViews", "cols", "sheetData", "autoFilter"])

for b in bad:
    print("MISMATCH " + b)
print(f"checks: {n}")
print(f"mismatches: {len(bad)}")
