' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' BAI2 -- the THIRD adapter and the third structural shape
' (docs/financial_adapters_design.md §20).
'
' NACHA is fixed-width, camt is hierarchical XML, and this is DELIMITED AND
' VARIABLE-LENGTH with a logical record that can span several physical ones. It
' is the first caller of §4's `delimited` location kind, which had existed in
' the enum with nothing using it.
'
' THE FOREIGN CORPUS WAS READ BEFORE A LINE OF THE ADAPTER WAS WRITTEN, which
' is the correction from the NACHA work where real files arrived last. It is
' why the framing is right rather than retrofitted, and it produced four facts
' no fixture of ours would have contained:
'   - THE RECORD SEPARATOR IS A SLASH, not a newline: one real file packs two
'     whole records onto a line.
'   - a record whose last field is free text often has NO terminator at all,
'     running to the end of the line: 102 of 116 records in one sample.
'   - that text CONTAINS SLASHES, which shatters a reader splitting on each one.
'   - AN `88` CONTINUES THE PREVIOUS RECORD'S FIELD LIST, not its text -- the
'     defining feature of the format, and the one this adapter first got wrong.
'
' THE LOAD-BEARING TIER IS `framings`, and it is a DIFFERENCE: the same logical
' file written three physically different ways must give one answer. Asserting
' any single form passes on a reader that handles only that form.

load finio
load finio_bai2

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

function doc_of(reg, path)
    return finio.read_file(reg, path, {})
end function

function shape_of(reg, path)
    doc = finio.read_file(reg, path, {})
    nt = 0
    total = 0
    for each g in doc.entities.groups
        for each a in g.accounts
            nt = nt + count(a.transactions)
            total = total + finio_bai2.account_total(doc.records, a).total
        end for
    end for
    return (string(count(doc.entities.groups)) + "/" + string(nt) + "/" + string(total)
            + "/" + string(count(finio.validate(reg, doc))))
end function

reg = finio.registry([ finio_bai2.adapter() ])
dir = "tests/finio/bai2/"
fdir = "tests/finio/foreign_bai2/"

print "-- RECOGNITION"
r = finio_bai2.recognise(slurp(dir + "statement.bai"))
check("a BAI2 file is recognised exactly", r.classification, "exact")
check("and the revision comes from the version field", r.revision, "2")
check("and the framing it declares is terminated", r.framing, "terminated")
check("by a slash", r.record_terminator, "/")
check("a CSV is not recognised",
      finio_bai2.recognise("date,amount\n2026-09-15,125.00\n").classification, "unknown")
check("an empty source is not recognised", finio_bai2.recognise("").classification, "unknown")

print ""
print "-- THREE PHYSICAL FORMS, ONE LOGICAL FILE"
' THE TIER THIS ADAPTER EXISTS FOR. `statement` is one record per line;
' `packed` puts two records on each line separated only by the slash; and
' `continued` moves each account's summary onto 88 continuation records. A
' newline-per-record reader merges the packed pairs into nonsense, and a reader
' that treats an 88 as text loses every amount on one.
a = shape_of(reg, dir + "statement.bai")
b = shape_of(reg, dir + "packed.bai")
c = shape_of(reg, dir + "continued.bai")
check("one record per line and two records per line read the same", a, b)
check("and so does the same file with its summaries on continuations", a, c)
check("and the shared answer is the right one", a, "1/4/9780000/0")
' A SLASH INSIDE A TEXT FIELD is the crux trap of this format, since the slash
' is also the record terminator. A reader that splits on every one shatters the
' record into fragments -- and the fragments are not records, so they are
' counted, mis-typed, and the account total loses the amount that came with
' them. Controlled here because the foreign file that shows it (sample5) has
' other disagreements of its own, so it could not tell a fold defect from them.
slashed = doc_of(reg, dir + "statement.bai")
tx = slashed.records[8].fields
check("a transaction whose text holds a slash keeps all of it",
      tx.text.value, "FX USD/EUR SETTLEMENT")
check("and its amount is still its own", string(tx.amount.value), "275000")
check("and the file still totals correctly",
      count(finio.validate(reg, slashed)), 0)
check("the three really are different files",
      byte_count(slurp(dir + "statement.bai")) != byte_count(slurp(dir + "packed.bai"))
      and byte_count(slurp(dir + "packed.bai")) != byte_count(slurp(dir + "continued.bai")), true)

print ""
print "-- A CONTINUATION EXTENDS THE FIELD LIST, NOT THE TEXT"
' The defining feature of the format, and the thing this adapter got wrong
' first. Read as text, an account whose summary continues onto an 88 loses
' every amount on that record -- and the total that comes out is an ordinary
' number, merely too small.
cd = finio.read_file(reg, dir + "continued.bai", {})
acct = cd.entities.groups[0].accounts[0]
sums = cd.records[acct.identifier].fields.summaries
check("an account whose summary spans two records still has both groups",
      count(sums), 2)
check("and the second group's amount came off the continuation",
      string(sums[1].amount.value), "2830000")
check("the continuation record knows what it continues",
      cd.records[acct.identifier + 1].continues, acct.identifier)
check("and it is still a record the trailers count",
      cd.records[acct.identifier + 1].kind, "continuation")

print ""
print "-- THE THREE-LEVEL CONTROL TOTAL (§15)"
doc = finio.read_file(reg, dir + "statement.bai", {})
check("the file agrees with its own trailers at every level",
      count(finio.validate(reg, doc)), 0)
bad = finio.read_file(reg, dir + "bad_total.bai", {})
bi = finio.validate(reg, bad)
check("the corrupted file still READS", count(bad.records), count(doc.records))
check("and reports exactly one disagreement", count(bi), 1)
check("at the level that was corrupted", bi[0].code, "group_control_total")
check("naming both sides", contains(bi[0].message, "9790000") and contains(bi[0].message, "9780000"), true)
check("and where it is", contains(bi[0].message, "row "), true)

print ""
print "-- LOCATIONS ARE `delimited`, THE KIND NOTHING USED BEFORE"
loc = doc.records[2].fields.customer_account_number.location
check("a BAI2 location's kind is delimited", loc.kind, "delimited")
check("it carries a row", loc.row, 2)
check("and a column", loc.column, 1)
check("and it renders", finio.describe_location(loc), "row 2, column 1")
' CONTROL: the other two adapters' locations are different kinds, so "locations
' have a kind" is a distinction rather than a constant.
check("and the three kinds in use are three different kinds",
      loc.kind != "fixed_width" and loc.kind != "xml", true)

print ""
print "-- TEXT WITH COMMAS IN IT"
' The transaction text is the LAST field and real files put commas in it
' constantly. Read as a single comma-delimited field it truncates at the first
' comma and yields a plausible shorter description.
tx = doc.records[4].fields
check("a transaction's text keeps everything after its field",
      tx.text.value, "CHECK PAID, SERIAL 10231")
check("and the amount before it is still right", string(tx.amount.value), "-125000")
check("and a negative amount is negative, not unreadable", tx.amount.status, "ok")

print ""
print "-- FILES THIS PROJECT DID NOT WRITE"
' Three of the six validate completely clean, including THE SPECIFICATION'S OWN
' WORKED EXAMPLE, whose arithmetic can be checked by hand: 500000 + 70000000 +
' 1500000 = 72000000, which is what its account trailer declares.
spec = finio.read_file(reg, fdir + "spec-section3.txt", {})
check("the specification's own example validates clean",
      count(finio.validate(reg, spec)), 0)
check("and its account total is the one the spec prints",
      string(finio_bai2.account_total(spec.records, spec.entities.groups[0].accounts[0]).total),
      "72000000")
clean = 0
read_ok = 0
for each nm in [ "sample1.txt", "sample2.txt", "sample3.txt",
                 "sample4-continuations-newline-delimited.txt",
                 "sample5-issue113.txt", "spec-section3.txt" ]
    on error goto next
    d = finio.read_file(reg, fdir + nm, {})
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
check("all six foreign files read", read_ok, 6)
check("three of them validate clean", clean, 3)
check("and three do not", 6 - clean, 3)
' THE ONE WITH BLOCK PADDING reports exactly that and nothing else: the padding
' line is not a BAI2 record, so it is reported once AND left out of the counts
' the trailers state -- otherwise one cause produces four findings, three of
' them pointing away from it.
s3 = finio.read_file(reg, fdir + "sample3.txt", {})
check("a file carrying block padding reports the padding",
      join(codes_of(reg, s3), ","), "record_code")
check("and its record counts still agree", contains(codes_of(reg, s3), "file_record_count"), false)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
