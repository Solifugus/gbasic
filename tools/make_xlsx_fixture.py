#!/usr/bin/env python3
"""Build examples/fixtures/xlsx/basic.xlsx — the Stage 1 read fixture.

Committed as a binary because an .xlsx is a ZIP and cannot be diffed usefully;
this script is how it was made and how it is regenerated. Python is used ONLY
here, never in the test path (tests/run_xlsx.sh does not invoke it), so the
suite has no python3 dependency.

Nothing in it is arbitrary. Each element exists to pin a behaviour:

  * shared strings, one with an XML entity (&amp;) and one non-ASCII (café)
  * a SPARSE sheet: no row 4, no column C -- absence must stay absence
  * a formula cell carrying BOTH <f> and Excel's cached <v>
  * a boolean, an Excel error (#DIV/0!), an inline string
  * style indices pointing at money and date number formats
  * a vendor part nothing models, to prove the part tree retains it
  * two sheets whose tab order is resolved via r:id, not by filename
"""
import zipfile, os

parts = {}
parts['[Content_Types].xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet3.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet4.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/><Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/></Types>'''
parts['_rels/.rels'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>'''
parts['xl/workbook.xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Ledger" sheetId="1" r:id="rId1"/><sheet name="Notes &amp; Refs" sheetId="2" r:id="rId2"/><sheet name="Formulas" sheetId="3" r:id="rId5"/><sheet name="Circular" sheetId="4" r:id="rId6"/></sheets></workbook>'''
parts['xl/_rels/workbook.xml.rels'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/><Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/><Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet3.xml"/><Relationship Id="rId6" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet4.xml"/></Relationships>'''
parts['xl/sharedStrings.xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count="4" uniqueCount="4"><si><t>Account</t></si><si><t>Balance</t></si><si><t>Opening &amp; carry</t></si><si><t>café</t></si></sst>'''
parts['xl/styles.xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><numFmts count="1"><numFmt numFmtId="164" formatCode="&quot;$&quot;#,##0.00"/></numFmts><cellXfs count="3"><xf numFmtId="0" xfId="0"/><xf numFmtId="164" xfId="0" applyNumberFormat="1"/><xf numFmtId="14" xfId="0" applyNumberFormat="1"/></cellXfs></styleSheet>'''
parts['xl/worksheets/sheet1.xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><dimension ref="A1:D7"/><sheetData><row r="1"><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row><row r="2"><c r="A2" t="s"><v>2</v></c><c r="B2" s="1"><v>1250.5</v></c></row><row r="3"><c r="A3" t="s"><v>3</v></c><c r="B3" s="1"><v>-99.25</v></c></row><row r="5"><c r="A5" t="inlineStr"><is><t>Total</t></is></c><c r="B5" s="1"><f>SUM(B2:B3)</f><v>1151.25</v></c></row><row r="6"><c r="A6" s="2"><v>45000</v></c><c r="B6" t="b"><v>1</v></c></row><row r="7"><c r="A7" t="e"><v>#DIV/0!</v></c><c r="D7"><f>B5*2</f><v>2302.5</v></c></row></sheetData></worksheet>'''
parts['xl/worksheets/sheet2.xml'] = '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1" t="inlineStr"><is><t>second sheet</t></is></c></row></sheetData></worksheet>'''
# A formula sheet, for the evaluator and the xlsx.check oracle.
#
# NOTE ON THE CACHED VALUES: they are hand-computed here, so `xlsx.check`
# against THIS fixture measures self-consistency, not conformance to Excel. It
# becomes a true oracle only when pointed at a workbook Excel actually wrote.
#
# Included on purpose: operator precedence, a parenthesised override, unary
# minus, postfix percent, exponent, division by zero, concatenation,
# comparison, IF, ROUND (half away from zero), the range aggregates, COUNT
# skipping text, IFERROR, a VOLATILE function (must be skipped, not judged),
# and an UNSUPPORTED one (must be named, not defaulted to a plausible zero).
_f = [
  '<row r="1"><c r="A1"><v>10</v></c><c r="B1"><f>A1+A2*A3</f><v>20</v></c></row>',
  '<row r="2"><c r="A2"><v>4</v></c><c r="B2"><f>(A1+A2)*A3</f><v>35</v></c></row>',
  '<row r="3"><c r="A3"><v>2.5</v></c><c r="B3"><f>A1/A2</f><v>2.5</v></c></row>',
  '<row r="4"><c r="A4" t="inlineStr"><is><t>7</t></is></c>'
  '<c r="B4" t="e"><f>A1/0</f><v>#DIV/0!</v></c></row>',
  '<row r="5"><c r="B5"><f>2^10</f><v>1024</v></c></row>',
  '<row r="6"><c r="B6"><f>-A2+1</f><v>-3</v></c></row>',
  '<row r="7"><c r="B7"><f>50%*A1</f><v>5</v></c></row>',
  '<row r="8"><c r="B8" t="str"><f>"x"&amp;A1</f><v>x10</v></c></row>',
  '<row r="9"><c r="B9" t="b"><f>A1&gt;A2</f><v>1</v></c></row>',
  '<row r="10"><c r="B10" t="str"><f>IF(A1&gt;A2,"big","small")</f><v>big</v></c></row>',
  '<row r="11"><c r="B11"><f>ROUND(A3,0)</f><v>3</v></c></row>',
  '<row r="12"><c r="B12"><f>SUM(A1:A3)</f><v>16.5</v></c></row>',
  '<row r="13"><c r="B13"><f>AVERAGE(A1:A3)</f><v>5.5</v></c></row>',
  '<row r="14"><c r="B14"><f>MIN(A1:A3)</f><v>2.5</v></c></row>',
  '<row r="15"><c r="B15"><f>MAX(A1:A3)</f><v>10</v></c></row>',
  '<row r="16"><c r="B16"><f>COUNT(A1:A4)</f><v>3</v></c></row>',
  '<row r="17"><c r="B17" t="str"><f>IFERROR(A1/0,"caught")</f><v>caught</v></c></row>',
  '<row r="18"><c r="B18"><f>NOW()</f><v>45870.5</v></c></row>',
  # An UNSUPPORTED function, which must be reported BY NAME rather than
  # defaulted to a plausible zero. This was XLOOKUP until XLOOKUP was
  # implemented, at which point the test asserting "at least one unsupported"
  # started failing -- a fixture pinned to a gap that later closed.
  #
  # An add-in call cannot suffer that: `_xll.` names a third-party XLL and is
  # unevaluable IN PRINCIPLE, not merely unimplemented. It is also the real
  # shape from the corpus, where _xll.HPVAL alone appears 9,240 times, and it
  # doubles as the guard that the future-function prefix stripper does NOT
  # strip `_xll.` -- doing so would turn this into a call to a function named
  # HPVAL and quietly reclassify "impossible" as "not done yet".
  '<row r="19"><c r="B19"><f>_xll.HPVAL(A1)</f><v>10</v></c></row>',
]
parts['xl/worksheets/sheet3.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
    '<sheetData>' + ''.join(_f) + '</sheetData></worksheet>')

# A deliberate CIRCULAR REFERENCE, on its own sheet so it cannot skew the
# Formulas statistics. Excel only iterates toward a fixed point when the user
# opts in; the default is to report the cycle, which is what recalc does.
parts['xl/worksheets/sheet4.xml'] = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
    '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
    '<sheetData>'
    '<row r="1"><c r="A1"><f>B1+1</f><v>0</v></c><c r="B1"><f>A1+1</f><v>0</v></c></row>'
    '<row r="2"><c r="A2"><v>5</v></c><c r="B2"><f>A2*3</f><v>15</v></c></row>'
    '</sheetData></worksheet>')

parts['xl/customData/vendor.xml'] = '''<?xml version="1.0" encoding="UTF-8"?><vendorBlob who="not-modelled"><keep>this must survive a round trip</keep></vendorBlob>'''

out = os.path.join(os.path.dirname(__file__), '..', 'examples', 'fixtures', 'xlsx', 'basic.xlsx')
out = os.path.normpath(out)
os.makedirs(os.path.dirname(out), exist_ok=True)
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for name, body in parts.items():
        z.writestr(name, body)
print("wrote", out, os.path.getsize(out), "bytes,", len(parts), "parts")
