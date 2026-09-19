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

' A document whose containment collapsed has NO statements, and reaching into
' one is an index error that kills the fixture BEFORE the tier that would have
' named the problem. Reported as a mismatch instead -- the failure class this
' project has now hit twice.
function first_txn_field(doc, name)
    if count(doc.entities.statements) = 0 then
        return { status: "NO-STATEMENTS", value: "NO-STATEMENTS", raw: "NO-STATEMENTS",
                 why: "the document produced no statements at all" }
    end if
    st = doc.entities.statements[0]
    if count(st.transactions) = 0 then
        return { status: "NO-TRANSACTIONS", value: "NO-TRANSACTIONS", raw: "NO-TRANSACTIONS",
                 why: "the statement produced no transactions at all" }
    end if
    flds = doc.records[st.transactions[0]].fields
    ' AND A MISSING FIELD IS AN ANSWER, not an index error. A classification
    ' defect that turns a leaf into an aggregate removes the field entirely, and
    ' reaching for it kills the fixture at the exact check written to report it.
    if not has(flds, name) then
        return { status: "NO-SUCH-FIELD", value: "NO-SUCH-FIELD", raw: "NO-SUCH-FIELD",
                 why: ("no field `" + name + "` -- was it read as an aggregate?") }
    end if
    return flds[name]
end function

function txn_count(doc)
    if count(doc.entities.statements) = 0 then
        return -1
    end if
    return count(doc.entities.statements[0].transactions)
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
print "-- AN AMOUNT IS `money`, AND THIS TIER EXISTS BECAUSE A REAL FILE FOUND IT"
' Every value this adapter produced used to be TEXT, and it was the only one of
' the five adapters that did -- NACHA, camt.053 and pain.001 all hand an amount
' back as `money`. Nothing here caught it, and the reason is worth recording:
' the checks above all go through `string(...)`, and `string(money)` and the
' text it was parsed from are THE SAME CHARACTERS. A suite written by the hands
' that wrote the reader cannot see a defect that only shows when somebody does
' arithmetic.
tv1 = finio.read_file(reg, dir + "statement_v1.ofx", {})
amt = tv1.records[tv1.entities.statements[0].transactions[0]].fields.TRNAMT
check("a transaction amount is money, not text", type(amt.value), "money")
check("and it is denominated by the statement's own CURDEF", amt.currency, "USD")
check("a balance is money too", type(tv1.records[tv1.entities.statements[0].balances[0]].fields.BALAMT.value), "money")
' THE LOAD-BEARING CHECK, and it is the failure itself rather than the type:
' added as text these gave "1250.00-87.45", a plausible-looking string, with
' nothing raised. Asserted as the ARITHMETIC so it cannot pass on a value that
' merely reports the right `type`.
t0 = tv1.records[tv1.entities.statements[0].transactions[0]].fields.TRNAMT.value
t1 = tv1.records[tv1.entities.statements[0].transactions[1]].fields.TRNAMT.value
check("two amounts ADD rather than concatenate", string(t0 + t1), "1162.55")
' THE CONTROL: the bank's characters are still there for a caller that wants
' them, which is Axiom 2 and is what `raw` is for. Without this, "it is money"
' would be satisfied by a reader that had thrown the source text away.
check("and the bank's own characters are kept", trim(amt.raw), "1250.00")
' `raw` is the SOURCE BYTES and is deliberately NOT trimmed -- an SGML leaf runs
' to the next `<`, so it carries the line break and the indentation after it.
' That is Axiom 2 working: the trimmed form is a reading, the raw form is what
' arrived. Asserted because a reader "helpfully" trimming raw would lose the one
' thing raw is for.
check("and raw really is untrimmed source, not a second reading",
      amt.raw = trim(amt.raw), false)
' A NON-AMOUNT IS UNTOUCHED. Dates stay text on purpose -- camt leaves
' `BookgDt` and `CreDtTm` as text and so does every other adapter -- and an OFX
' date additionally carries a bank-stated zone, which this adapter has no
' business resolving. Without this check, "amounts are typed" would be
' indistinguishable from "everything is typed".
dtp = tv1.records[tv1.entities.statements[0].transactions[0]].fields.DTPOSTED
check("a date is still the bank's text", type(dtp.value), "string")

print ""
print "-- AN AMOUNT IN NO CURRENCY IS `invalid`, NOT A NUMBER"
' camt already refuses an `<Amt>` with no Ccy for this reason: a figure with no
' currency is not an amount, and answering with the digits alone invents one.
' THE LINE IS REMOVED, not emptied. Emptying it would make this tier depend on
' how an EMPTY ELEMENT is classified, which is a different question tested
' below -- and when that classification broke, this tier died on an index
' before the one written for it could speak.
nocur = replace(slurp(dir + "statement_v1.ofx"), chr(9) + chr(9) + chr(9) + chr(9) + "<CURDEF>USD" + chr(10), "")
nd = finio.read_text(reg, nocur, {})
na = first_txn_field(nd, "TRNAMT")
check("with no CURDEF the amount is invalid", na.status, "invalid")
check("and it says why", contains(na.why, "no CURDEF"), true)
check("and the raw text survives for a reader to see", trim(na.raw), "1250.00")
check("validation reports it", contains(codes_of(reg, nd), "amount_not_decimal"), true)
' THE CONTROL: the same file WITH its CURDEF is fine, or "invalid" would be
' satisfied by an adapter that refused every amount there is.
check("the same statement with its CURDEF is ok", amt.status, "ok")
check("and reports nothing", contains(codes_of(reg, tv1), "amount_not_decimal"), false)

print ""
print "-- AN ELEMENT WITH NO CONTENT, WHICH IS A LEAF AND NOT AN AGGREGATE"
' SGML cannot tell the two apart without a DTD. The rule used to be "empty means
' aggregate", and in 1.x -- where a leaf has NO closing tag -- that pushes a name
' nothing will ever pop: every close after it mismatches, containment collapses,
' and the document comes back with ZERO STATEMENTS AND NO ERROR. Measured before
' the fix: 9 records, 0 statements, nothing raised.
'
' NO FILE IN THIS CORPUS HAS ONE -- not the ten foreign vectors, not the real
' credit-union download that led here -- so this was found by CONSTRUCTION. It
' is asserted anyway, because an empty `<MEMO>` or `<CHECKNUM>` is ordinary in
' 1.x and the failure is silent and total.
e1 = replace(slurp(dir + "statement_v1.ofx"), "<TRNAMT>", "<CHECKNUM>" + chr(10) + "<TRNAMT>")
e2 = replace(slurp(dir + "statement_v2.ofx"), "<TRNAMT>", "<CHECKNUM></CHECKNUM>" + chr(10) + "<TRNAMT>")
d1 = finio.read_text(reg, e1, {})
d2 = finio.read_text(reg, e2, {})
base = finio.read_text(reg, slurp(dir + "statement_v1.ofx"), {})
nb = txn_count(base)
check("an empty element does not cost the 1.x file its transactions",
      txn_count(d1), nb)
check("nor the 2.x file", txn_count(d2), nb)
' THE LOAD-BEARING HALF, and it is this adapter's whole claim: the two
' serializations of ONE logical statement must give ONE answer. Asserting either
' alone passes on a reader that handles that dialect and mangles the other.
check("and both dialects agree about the empty element",
      first_txn_field(d1, "CHECKNUM").status,
      first_txn_field(d2, "CHECKNUM").status)
check("which is `ok` with nothing in it, not a missing field",
      first_txn_field(d1, "CHECKNUM").value, "")
' THE CONTROL: a genuinely empty AGGREGATE is still an aggregate. Without it the
' fix would be satisfied by a reader that had stopped nesting altogether.
agg = finio.read_text(reg, replace(slurp(dir + "statement_v1.ofx"),
        "<BANKACCTFROM>", "<SECLIST>" + chr(10) + "</SECLIST>" + chr(10) + "<BANKACCTFROM>"), {})
check("an empty aggregate is still nested, not flattened into a field",
      txn_count(agg), nb)

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
