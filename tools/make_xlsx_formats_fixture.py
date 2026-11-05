#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/formats.xlsx and formats1904.xlsx, and the
answers tests/xlsx_formats/formats_test.bas checks them against
(examples/fixtures/xlsx/formats.expected).

Gap G13 from the gBASIC books: a cell's NUMBER FORMAT is what says a number is
a date. Each cell below is a serial under one format, and its expected kind is
written here by hand from the format code; its expected VALUE is computed here,
by Python's datetime from the serial and the workbook's epoch, not by gBASIC.
The epochs, from ECMA-376 Part 1 §18.17.4.1: the 1900 system counts from
1899-12-31 as day 0 and has a day 60 (1900-02-29) that never was, so from 61
on a serial is days after 1899-12-30; the 1904 system counts from 1904-01-01.

Committed, with its output, as basic.xlsx is: python3 is not in the test path.
"""
import datetime as dt
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "examples", "fixtures", "xlsx")

# (ref, serial, builtin id or custom code, expected kind)
CELLS = [
    ("A1", 45000, 14, "date"),                     # built-in mm-dd-yy
    ("A2", 45000.25, 22, "datetime"),              # built-in m/d/yy h:mm
    ("A3", 0.5625, 20, "time"),                    # h:mm
    ("A4", 1.2604166666666667, 46, "duration"),    # [h]:mm:ss, 30:15:00
    ("A5", 0.15, 9, "percent"),                    # 0%
    ("A6", 0.125, 10, "percent"),                  # 0.00%
    ("A7", 42, 0, "number"),                       # General, whole: exact
    ("A8", 1234.5, 4, "number"),                   # #,##0.00
    ("A9", 0.0034722222222222225, 45, "time"),     # mm:ss: m is MINUTES before s
    ("A10", 45000, "yyyy-mm-dd", "date"),
    ("A11", 45000.999988425926, "dd/mm/yyyy hh:mm:ss", "datetime"),  # 23:59:59
    ("A12", 0.10416666666666667, "[h]:mm", "duration"),              # 2:30
    ("A13", 0.153, "0.0%", "percent"),
    ("A14", 45000, '"m"0', "number"),              # an m in quotes is text
    ("A15", 45000, "mmm-yy", "date"),              # m alone is MONTH
    ("A16", 59, 14, "date"),                       # 1900-02-28
    ("A17", 60, 14, "number"),                     # 1900-02-29, which never was
    ("A18", 61, 14, "date"),                       # 1900-03-01
    ("A19", 1, 14, "date"),                        # 1900-01-01
    ("A20", 45000, "[$-409]mmmm d, yyyy", "date"), # a locale tag is skipped
    ("A21", 45000, "#,##0;[Red]-#,##0", "number"), # a colour is skipped
    ("A22", 0.75, "h:mm AM/PM", "time"),           # the M of AM is not a month
    ("A23", -1, 14, "number"),                     # a negative date shows ####
    ("A24", 1.5, "[mm]:ss", "duration"),           # elapsed minutes
    ("A25", 45000, "[Red]0.00", "number"),         # a colour's letters (the d of Red) are not a format
]
CELLS_1904 = [
    ("A1", 43538, 14, "date"),                     # the same day, 1904 system
    ("A2", 0, 14, "date"),                         # 1904-01-01 itself
]


def expected(serial, kind, system1904):
    if kind in ("number", "percent"):
        return repr(serial)
    if kind == "duration":
        return str(round(serial * 86400))
    if kind == "time":
        s = round((serial - int(serial // 1)) * 86400) % 86400
        return "%02d:%02d:%02d" % (s // 3600, s // 60 % 60, s % 60)
    day = int(serial // 1)
    secs = round((serial - day) * 86400)
    if secs >= 86400:
        day, secs = day + 1, secs - 86400
    if system1904:
        base = dt.datetime(1904, 1, 1)
    elif day >= 61:
        base = dt.datetime(1899, 12, 30)
    else:
        base = dt.datetime(1899, 12, 31)
    t = base + dt.timedelta(days=day, seconds=secs)
    return t.strftime("%Y-%m-%d") if kind == "date" else t.strftime("%Y-%m-%d %H:%M:%S")


def build(name, cells, system1904):
    customs, xfs = [], []
    for _, _, fmt, _ in cells:
        if isinstance(fmt, int):
            fid = fmt
        else:
            if fmt not in [c for _, c in customs]:
                customs.append((164 + len(customs), fmt))
            fid = [i for i, c in customs if c == fmt][0]
        if fid not in xfs:
            xfs.append(fid)
    esc = lambda s: s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;")
    numfmts = "".join('<numFmt numFmtId="%d" formatCode="%s"/>' % (i, esc(c)) for i, c in customs)
    cellxfs = "".join('<xf numFmtId="%d" xfId="0" applyNumberFormat="1"/>' % f for f in xfs)
    rows = []
    for ref, serial, fmt, _ in cells:
        fid = fmt if isinstance(fmt, int) else [i for i, c in customs if c == fmt][0]
        rows.append('<row r="%s"><c r="%s" s="%d"><v>%r</v></c></row>' % (ref[1:], ref, xfs.index(fid), serial))
    parts = {
        "[Content_Types].xml": '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>',
        "_rels/.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
        "xl/workbook.xml": '<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">%s<sheets><sheet name="Formats" sheetId="1" r:id="rId1"/></sheets></workbook>' % ('<workbookPr date1904="1"/>' if system1904 else '<workbookPr/>'),
        "xl/_rels/workbook.xml.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>',
        "xl/styles.xml": '<?xml version="1.0" encoding="UTF-8"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="%d">%s</numFmts><cellXfs count="%d">%s</cellXfs></styleSheet>' % (len(customs), numfmts, len(xfs), cellxfs),
        # Excel's spacing: CRLF after the declaration, nothing after the root
        # (gap G17: a rebuilt part must keep both)
        "xl/worksheets/sheet1.xml": '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\r\n<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1:A%d"/><sheetData>%s</sheetData><pageMargins left="0.7" right="0.7" top="0.75" bottom="0.75" header="0.3" footer="0.3"/></worksheet>' % (len(rows), "".join(rows)),
    }
    with zipfile.ZipFile(os.path.join(OUT, name), "w", zipfile.ZIP_DEFLATED) as z:
        for k, v in parts.items():
            zi = zipfile.ZipInfo(k, date_time=(2026, 11, 5, 0, 0, 0))
            z.writestr(zi, v, zipfile.ZIP_DEFLATED)
    return ["%s %s %s %s %s" % (name, ref, kind, expected(serial, kind, system1904), repr(serial))
            for ref, serial, _, kind in cells]


lines = build("formats.xlsx", CELLS, False) + build("formats1904.xlsx", CELLS_1904, True)

# Gap G16: recalc's `changed` counted a formula whose double differs from its
# cached text only past Excel's 15 digits. A1*A2 is 0.30000000000000004 in
# binary and Excel caches 0.3; nothing changed, and recalc must say 0.
noise = {
    "[Content_Types].xml": '<?xml version="1.0" encoding="UTF-8"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>',
    "_rels/.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>',
    "xl/workbook.xml": '<?xml version="1.0" encoding="UTF-8"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Noise" sheetId="1" r:id="rId1"/></sheets></workbook>',
    "xl/_rels/workbook.xml.rels": '<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>',
    "xl/worksheets/sheet1.xml": '<?xml version="1.0" encoding="UTF-8"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><v>0.1</v></c><c r="B1"><v>3</v></c><c r="C1"><f>A1*B1</f><v>%r</v></c><c r="D1"><f>B1*10</f><v>30</v></c></row></sheetData></worksheet>' % round(0.1 * 3, 15),
}
assert 0.1 * 3 != round(0.1 * 3, 15)
with zipfile.ZipFile(os.path.join(OUT, "recalc_noise.xlsx"), "w", zipfile.ZIP_DEFLATED) as z:
    for k, v in noise.items():
        z.writestr(zipfile.ZipInfo(k, date_time=(2026, 11, 5, 0, 0, 0)), v, zipfile.ZIP_DEFLATED)
with open(os.path.join(OUT, "formats.expected"), "w") as f:
    f.write("\n".join(lines) + "\n")
