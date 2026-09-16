' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' pain.001 -- the FIFTH adapter, and THE FIRST FOR A FILE YOU SEND
' (docs/financial_adapters_design.md §16, §20).
'
' Every adapter before this one reads a report: an ACH file, a statement, a
' balance report, an OFX download. All four say some version of "re-emission
' only" for `write_status`. A pain.001 is an INSTRUCTION -- the message a
' business sends its bank to say "make these payments" -- and that inverts
' where the risk lives. Reading a statement wrongly gives a wrong number on a
' screen; writing a pain.001 wrongly gives a payment run the bank rejects, or
' executes.
'
' WHICH IS WHY §16 HAD NEVER BEEN IMPLEMENTED. "Writing requires stronger
' guarantees than reading... classify the requested conversion as
' representable, lossy or impossible... The default should favor refusal when
' semantic information would be silently lost." Four read-only adapters put no
' weight on that sentence.
'
' THE LOAD-BEARING TIER IS `write_classification`, and it is a THREE-WAY
' DIFFERENCE: the same reader accepts all three documents and the writer treats
' them differently -- one written, one refused-but-overridable, one refused with
' no override. Asserting any single one passes on a writer that always does
' that.

load finio
load finio_pain001

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

function wrote(reg, doc, allow)
    on error goto next
    out = finio.write_text(reg, doc, allow)
    if error then
        msg = error.message
        error.clear()
        on error stop
        return "REFUSED:" + msg
    end if
    on error stop
    return "WROTE:" + out.classification
end function

reg = finio.registry([ finio_pain001.adapter() ])
dir = "tests/finio/pain/"
fdir = "tests/finio/foreign_pain/"

print "-- RECOGNITION, AND A SIBLING MESSAGE IS NOT THIS ONE"
r = finio_pain001.recognise(slurp(dir + "payments.xml"))
check("a pain.001 is recognised exactly", r.classification, "exact")
check("and the revision comes from the namespace", r.revision, "pain.001.001.03")
' pain.008 is a DIRECT DEBIT in the same family and the same namespace scheme.
' Reading one as a credit transfer would find no CdtTrfTxInf and report an
' empty instruction -- which looks exactly like a payment run with nothing in
' it, so it is refused by name rather than read.
p8 = finio_pain001.recognise(slurp(fdir + "pain008-sdd-core.xml"))
check("a pain.008 direct debit is NOT recognised", p8.classification, "unknown")
check("and the reason names what it actually is",
      contains(join(p8.reasons, " | "), "pain.008.001.02"), true)
check("a camt statement is not recognised either",
      finio_pain001.recognise(slurp("tests/finio/camt/statement.xml")).classification, "unknown")
check("an empty source is not recognised", finio_pain001.recognise("").classification, "unknown")

print ""
print "-- TWO LEVELS OF CONTROL TOTAL, CHECKED INDEPENDENTLY"
' The foreign corpus has only SINGLE-block messages, where the group total is a
' copy of the block's -- a two-level checksum tested only where the two levels
' are equal tests one level. These have two blocks.
doc = finio.read_file(reg, dir + "payments.xml", {})
check("two payment blocks", count(doc.entities.blocks), 2)
check("four transactions across them",
      count(doc.entities.blocks[0].transactions) + count(doc.entities.blocks[1].transactions), 4)
check("and the message agrees with itself at both levels",
      count(finio.validate(reg, doc)), 0)
bg = finio.read_file(reg, dir + "bad_group_sum.xml", {})
check("a wrong GROUP sum is caught", join(codes_of(reg, bg), ","), "message_control_sum")
bb = finio.read_file(reg, dir + "bad_block_sum.xml", {})
check("a wrong BLOCK sum is caught", join(codes_of(reg, bb), ","), "block_control_sum")
check("and the two are different findings, so the levels are independent",
      join(codes_of(reg, bg), ",") != join(codes_of(reg, bb), ","), true)

print ""
print "-- §16: THE SAME READER, THREE DIFFERENT WRITE VERDICTS"
' This is what four read-only adapters could not put under pressure. All three
' documents READ without complaint; what differs is whether they may be SENT.
ok_doc = finio.read_file(reg, dir + "payments.xml", {})
lossy = finio.read_file(reg, dir + "long_name.xml", {})
imposs = finio.read_file(reg, dir + "wrong_currency.xml", {})
check("all three read", count(ok_doc.records) > 0 and count(lossy.records) > 0 and count(imposs.records) > 0, true)
check("and the lossy one VALIDATES CLEAN -- it is a fine document",
      count(finio.validate(reg, lossy)), 0)
check("a representable document is written", left(wrote(reg, ok_doc, false), 6), "WROTE:")
check("a lossy one is REFUSED by default", left(wrote(reg, lossy, false), 8), "REFUSED:")
check("and the refusal names what would be lost",
      contains(wrote(reg, lossy, false), "would truncate"), true)
check("and offers the override", contains(wrote(reg, lossy, false), "allow_lossy"), true)
check("which works", left(wrote(reg, lossy, true), 6), "WROTE:")
check("and still reports the loss",
      count(finio.write_text(reg, lossy, true).loss), 1)
check("an impossible one is refused", left(wrote(reg, imposs, false), 8), "REFUSED:")
check("and has NO override, which is the difference from lossy",
      left(wrote(reg, imposs, true), 8), "REFUSED:")
check("and says so", contains(wrote(reg, imposs, true), "no override"), true)

print ""
print "-- THE LIMITS ARE THE SCHEME'S, NOT THE SCHEMA'S"
' An XSD accepts a 200-character creditor name; the SEPA scheme carries 70, and
' a bank truncates or rejects. An adapter that serialized it would produce a
' file that VALIDATES, gets sent, and comes back refused.
c = finio_pain001.classify_write(lossy)
check("the long name is classified lossy", c.classification, "lossy")
check("naming the field", c.losses[0].concept, "creditor")
check("its length", c.losses[0].length, 73)
check("and the scheme's limit", c.losses[0].limit, 70)
check("CONTROL: 70 is the limit and the document is otherwise ordinary",
      finio_pain001.scheme_limits().creditor_name, 70)
ci = finio_pain001.classify_write(imposs)
check("a non-EUR amount is impossible, not lossy", ci.classification, "impossible")
check("and the reason says no truncation helps",
      contains(ci.impossible[0].why, "no truncation makes it representable"), true)

print ""
print "-- A CONTROL SUM CARRIES NO CURRENCY OF ITS OWN"
' So it is only meaningful if every amount it totals is in the same one. A
' block mixing EUR and USD has a control sum that means nothing -- and `money`
' will not add them, so the mismatch has to be REPORTED rather than allowed to
' raise (§15: a malformed source is described, not died on).
mixed = finio.read_file(reg, dir + "mixed_currency.xml", {})
check("a block mixing currencies is reported",
      contains(codes_of(reg, mixed), "mixed_currency_block"), true)
check("and reading it does not raise", count(mixed.records) > 0, true)
check("CONTROL: the single-currency message reports no such thing",
      contains(codes_of(reg, doc), "mixed_currency_block"), false)

print ""
print "-- FILES THIS PROJECT DID NOT WRITE"
for each nm in [ "pain001-single-sct.xml", "pain001-batch-sct.xml" ]
    d = finio.read_file(reg, fdir + nm, {})
    check(nm + " validates clean", count(finio.validate(reg, d)), 0)
    check(nm + " is representable", finio_pain001.classify_write(d).classification, "representable")
end for

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
