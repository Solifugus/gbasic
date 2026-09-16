' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' OFX -- the FOURTH adapter, and §20's SCHEMA-DRIFT CASE OCCURRING INSIDE ONE
' FORMAT (docs/financial_adapters_design.md §20).
'
' OFX 1.x is SGML-like and NOT well-formed XML -- a leaf is `<CODE>0` with no
' closing tag -- while 2.x is proper XML. Same element tree, two
' serializations, and a file of each kind is ordinary. Until now schema drift
' had only arrived BETWEEN formats; this is the first time it is inside one.
'
' THE LOAD-BEARING TIER IS `dialects`, and it is a DIFFERENCE: the same logical
' statement written both ways must give ONE answer. Asserting either alone
' passes on a reader that handles only that one -- and the foreign corpus
' cannot supply the pair, because every real file is one dialect or the other.
'
' CONVERSION WAS REJECTED, which is why one reader serves both. Inserting the
' missing closing tags and handing the result to `xml.parse` is what more than
' one public tool does, and it would put every LOCATION into text the bank
' never sent. Axiom 2 says a value is traceable to its source; a byte offset
' into a document this library invented is not provenance.

load finio
load finio_ofx

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function slurp(path)
    f {file}= path
    return read(f)
end function

function codes_of(reg, doc)
    out = []
    for each i in finio.validate(reg, doc)
        if not contains(out, i.code) then
            append(out, i.code)
        end if
    end for
    return out
end function

function shape_of(reg, path)
    doc = finio.read_file(reg, path, {})
    nt = 0
    amounts = []
    fitids = []
    for each st in doc.entities.statements
        nt = nt + count(st.transactions)
        for each ti in st.transactions
            append(amounts, string(doc.records[ti].fields.TRNAMT.value))
            append(fitids, string(doc.records[ti].fields.FITID.value))
        end for
    end for
    return (string(count(doc.entities.statements)) + "|" + string(nt) + "|"
            + join(amounts, ",") + "|" + join(fitids, ",")
            + "|" + string(count(finio.validate(reg, doc))))
end function

reg = finio.registry([ finio_ofx.adapter() ])
dir = "tests/finio/ofx/"
fdir = "tests/finio/foreign_ofx/"

print "-- RECOGNITION KNOWS WHICH DIALECT IT IS LOOKING AT"
r1 = finio_ofx.recognise(slurp(dir + "statement_v1.ofx"))
r2 = finio_ofx.recognise(slurp(dir + "statement_v2.ofx"))
check("a 1.x file is recognised exactly", r1.classification, "exact")
check("and reported as sgml", r1.dialect, "sgml")
check("with its version from the OFXHEADER block", r1.revision, "102")
check("a 2.x file is recognised exactly", r2.classification, "exact")
check("and reported as xml", r2.dialect, "xml")
check("with its version from the <?OFX?> instruction", r2.revision, "211")
check("and the two dialects are DIFFERENT answers", r1.dialect != r2.dialect, true)
check("a CSV is not recognised",
      finio_ofx.recognise("date,amount\n2026-09-15,1.00\n").classification, "unknown")
check("an empty source is not recognised", finio_ofx.recognise("").classification, "unknown")

print ""
print "-- ONE STATEMENT, TWO SERIALIZATIONS, ONE ANSWER"
' The tier this adapter exists for. Every real file in the corpus is one
' dialect or the other, so the pair has to be generated -- and without the pair
' a reader that handled only one would pass everything.
a = shape_of(reg, dir + "statement_v1.ofx")
b = shape_of(reg, dir + "statement_v2.ofx")
check("the SGML and XML forms read identically", a, b)
check("and the shared answer is the right one", a,
      "1|3|1250.00,-87.45,-2000.00|FIT0001,FIT0002,FIT0003|0")
check("the two files really are different bytes",
      slurp(dir + "statement_v1.ofx") = slurp(dir + "statement_v2.ofx"), false)
check("and one really has closing tags where the other does not",
      contains(slurp(dir + "statement_v2.ofx"), "</TRNAMT>")
      and contains(slurp(dir + "statement_v1.ofx"), "</TRNAMT>") = false, true)

print ""
print "-- LOCATIONS POINT AT THE BYTES THE BANK SENT"
' Conversion to XML was rejected precisely so this holds for 1.x as well.
v1 = finio.read_file(reg, dir + "statement_v1.ofx", {})
loc = v1.records[v1.entities.statements[0].transactions[0]].fields.TRNAMT.location
check("a location's kind is xml -- a statement about SHAPE, not syntax", loc.kind, "xml")
check("it names the path", contains(loc.path, "STMTTRN/TRNAMT"), true)
check("and which occurrence of it", loc.occurrence, 0)
check("the second transaction's amount is a later occurrence",
      v1.records[v1.entities.statements[0].transactions[1]].fields.TRNAMT.location.occurrence, 1)

print ""
print "-- THE FITID, WHICH IS WHAT OFX CONSUMERS ACTUALLY DEPEND ON"
' OFX states NO control total -- the three formats before it all did, and it
' would have been easy to generalise. The ledger balance is a BALANCE, not a
' sum of anything in the file. So what validation can check is what a consumer
' relies on, and the FITID is the whole of deduplication: a duplicate means a
' transaction is dropped or counted twice, and nothing reports it.
dup = finio.read_file(reg, dir + "duplicate_fitid.ofx", {})
di = finio.validate(reg, dup)
check("a duplicate FITID is reported", contains(codes_of(reg, dup), "duplicate_fitid"), true)
check("once per repeat, not once for the set", count(di), 2)
check("and the message says what a consumer would do with it",
      contains(di[0].message, "will drop one of them"), true)
check("CONTROL: the same statement with distinct FITIDs is clean",
      count(finio.validate(reg, v1)), 0)

print ""
print "-- AN ERROR RESPONSE IS NOT AN EMPTY STATEMENT"
' A non-zero status code means the response failed. A reader that walks past it
' finds no transactions and reports a balance of nothing, which looks exactly
' like an account with no activity.
fail = finio.read_file(reg, fdir + "signon_fail.ofx", {})
fc = codes_of(reg, fail)
check("a failed signon is reported", contains(fc, "status_not_ok"), true)
err = finio.read_file(reg, fdir + "error_message.ofx", {})
check("and so is an error inside an otherwise-fine response",
      contains(codes_of(reg, err), "status_not_ok"), true)
check("even though its signon status is 0, so the check cannot stop at the first",
      err.records[0].fields.CODE.value, "0")

print ""
print "-- A STATEMENT KIND THIS ADAPTER DOES NOT MODEL IS REPORTED"
' `fidelity-savings.ofx` carries four transactions inside an INVSTMTRS
' investment statement. They are read and attached to nothing, and without this
' the file reports a clean bank statement with no activity -- indistinguishable
' from an account that had none.
inv = finio.read_file(reg, fdir + "fidelity-savings.ofx", {})
check("transactions belonging to no statement are reported",
      contains(codes_of(reg, inv), "unmodelled_statement"), true)
check("and the count is named", contains(finio.validate(reg, inv)[0].message, "4 transaction"), true)
check("CONTROL: a bank statement's transactions ARE attached, so this is not universal",
      contains(codes_of(reg, v1), "unmodelled_statement"), false)

print ""
print "-- FILES PUBLISHED BY REAL FINANCIAL INSTITUTIONS"
names = [ "anzcc.ofx", "bank_medium.ofx", "bank_small.ofx", "checking.ofx",
          "error_message.ofx", "fidelity-savings.ofx", "multiple_accounts.ofx",
          "ofx-v102-empty-tags.ofx", "signon_fail.ofx", "suncorp.ofx" ]
read_ok = 0
clean = 0
sgml_n = 0
xml_n = 0
for each nm in names
    t = slurp(fdir + nm)
    rr = finio_ofx.recognise(t)
    if rr.dialect = "sgml" then
        sgml_n = sgml_n + 1
    end if
    if rr.dialect = "xml" then
        xml_n = xml_n + 1
    end if
    on error goto next
    d = finio.read_text(reg, t, {})
    if error then
        error.clear()
        on error stop
    else
        on error stop
        read_ok = read_ok + 1
        if count(finio.validate(reg, d)) = 0 then
            clean = clean + 1
        end if
    end if
end for
check("all ten read", read_ok, 10)
check("and the corpus carries BOTH dialects, which is what makes it a test",
      sgml_n > 0 and xml_n > 0, true)
check("six validate clean", clean, 6)
check("and four do not", 10 - clean, 4)
' A REAL FILE OPENING WITH TWELVE BLANK LINES. Nothing here depends on the
' header being the first thing in the file, which a reader written from the
' specification alone would assume.
check("a file with leading blank lines is still recognised",
      finio_ofx.recognise(slurp(fdir + "ofx-v102-empty-tags.ofx")).classification, "exact")
check("an entirely empty <OFX></OFX> document reads and reports nothing",
      count(finio.validate(reg, finio.read_file(reg, fdir + "bank_small.ofx", {}))), 0)

print ""
print "-- §17: THE SOURCE IS RE-EMITTED, SO A 1.x FILE STAYS 1.x"
' Re-serializing a parsed 1.x document would produce 2.x, and a reader
' downstream expecting what the bank sent would get something else.
out = finio.write_text(reg, v1)
check("a 1.x document re-emits byte-identically", out.text = slurp(dir + "statement_v1.ofx"), true)
check("and the adapter declares that guarantee", out.byte_fidelity, true)
check("and it is still 1.x", contains(out.text, "</TRNAMT>"), false)

print ""
print "-- REFUSALS"
on error goto next
finio.read_text(reg, slurp(dir + "statement_v1.ofx"), { revision: "999" })
check("a version this adapter does not implement is refused",
      contains(error.message, "does not implement revision"), true)
error.clear()
on error stop

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
