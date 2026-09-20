' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_camt -- ISO 20022 camt.053, the SECOND adapter and the first one of a
' different representation (docs/financial_adapters_design.md §20, §21).
'
' §20's argument is that an abstraction surviving only one representation is a
' generalized parser rather than a description of the problem. THE TIER THAT
' ANSWERS THAT QUESTION IS `shape`, and it is the only one here that needs both
' adapters in one program: one walk over a fixed-width ACH file and a
' hierarchical XML statement, reaching the same fields by the same names. Every
' other tier checks a component.
'
' SELF-CHECKING RATHER THAN GOLDEN, forced the same way NACHA's is: every defect
' produces AN ORDINARY-LOOKING STATEMENT. A credit read as a debit still
' balances, on the other side. An entry dropped from a total gives a closing
' balance that is merely wrong. An amount read without its Ccy attribute is a
' perfectly good number in no currency.

load finio
load finio_nacha
load finio_camt

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

cdir = "tests/finio/camt/"
ndir = "tests/finio/nacha/"
reg = finio.registry([ finio_camt.adapter() ])
both = finio.registry([ finio_nacha.adapter(), finio_camt.adapter() ])
stmt_text = slurp(cdir + "statement.xml")

' ===========================================================================
print "-- RECOGNITION: the namespace carries the revision"
' ===========================================================================
' THE OPPOSITE ANSWER FROM NACHA, and that is why a second adapter was needed
' to test §7's resolution at all. A NACHA file does not say which rule book
' produced it, so that adapter reports no revision and the framework records
' that the source carried none. A camt document declares its version in the
' namespace, so the revision is DETERMINED FROM THE SOURCE.
r = finio_camt.recognise(stmt_text)
check("a camt.053 document is recognised exactly", r.classification, "exact")
check("and the revision comes from the namespace", r.revision, "camt.053.001.08")
check("and the reason names where it came from",
      contains(join(r.reasons, " | "), "namespace declares camt.053.001.08"), true)
check("CONTROL: the NACHA adapter reports no revision at all",
      is_unknown(finio_nacha.recognise(slurp(ndir + "payroll.ach")).revision), true)
check("an empty source is not recognised", finio_camt.recognise("").classification, "unknown")
check("XML that is not camt is not recognised",
      finio_camt.recognise(slurp(cdir + "not_camt.xml")).classification, "unknown")
' The namespace string could appear in a document ABOUT camt -- a schema, a
' mapping table, an email -- and answering `exact` for one of those is how an
' archive comes to be reported as full of statements nothing can read.
mention = "Please send the camt file. The namespace is urn:iso:std:iso:20022:tech:xsd:camt.053.001.08 as agreed."
check("a document merely MENTIONING the namespace is possible, not exact",
      finio_camt.recognise(mention).classification, "possible")
check("and the reason says the root element is missing",
      contains(join(finio_camt.recognise(mention).reasons, " | "), "no <Document> root"), true)

' ===========================================================================
print ""
print "-- A VERSION THIS ADAPTER DOES NOT IMPLEMENT IS REFUSED BY NAME"
' ===========================================================================
older = slurp(cdir + "older_version.xml")
check("the older document is still recognised", finio_camt.recognise(older).classification, "exact")
check("and its revision is read correctly", finio_camt.recognise(older).revision, "camt.053.001.02")
on error goto next
finio.read_text(reg, older, {})
check("but reading it is refused", contains(error.message, "does not implement revision"), true)
check("and the message names both the one asked for and the ones held",
      contains(error.message, "camt.053.001.02") and contains(error.message, "camt.053.001.08"), true)
error.clear()
on error stop

' ===========================================================================
print ""
print "-- THE STATEMENT'S OWN ARITHMETIC (§15)"
' ===========================================================================
' Opening plus what was credited less what was debited is the closing balance,
' and the optional summary states the same facts a second way. Both were
' computed by tools/make_camt_fixture.py, not by this adapter.
doc = finio.read_file(reg, cdir + "statement.xml", {})
check("two statements", count(doc.entities.statements), 2)
st0 = doc.entities.statements[0]
check("five entries on the first", count(st0.entries), 5)
check("two balances on it", count(st0.balances), 2)
check("and it is held in USD", st0.currency, "USD")
check("the document agrees with its own balances and summary",
      count(finio.validate(reg, doc)), 0)

bad = finio.read_file(reg, cdir + "bad_balance.xml", {})
bi = finio.validate(reg, bad)
check("the corrupted statement still READS", count(bad.entities.statements), 1)
check("and is still recognised exactly", bad.classification, "exact")
check("validation reports exactly one disagreement", count(bi), 1)
check("and names the movement", bi[0].code, "balance_movement")
check("and gives both sides",
      contains(bi[0].message, "13446.66") and contains(bi[0].message, "13456.66"), true)

' THE SUMMARY IS A SECOND STATEMENT OF THE SAME FACTS, so a reader that summed
' the entries wrongly and a file whose summary is wrong are told apart. Without
' checking it, a reader agreeing with the balances is only agreeing with one
' arithmetic.
broken_summary = replace(stmt_text, "<Sum>5550.10</Sum>", "<Sum>5550.11</Sum>")
sdoc = finio.read_text(reg, broken_summary, {})
scodes = []
for each i in finio.validate(reg, sdoc)
    append(scodes, i.code)
end for
check("a summary that disagrees with the entries is reported",
      contains(scodes, "summary_credit_total"), true)
check("and the balances, which are right, are NOT reported",
      contains(scodes, "balance_movement"), false)

' ===========================================================================
print ""
print "-- LOCATIONS ARE XML, NOT BYTE RANGES"
' ===========================================================================
' A hierarchical source has no byte range to give. `xml.parse` builds nodes
' carrying a name, a namespace, attributes and children and NO POSITION, so an
' adapter asked for `byte_offset` must invent one, answer `unknown`, or say
' where the value really is -- and only the third is provenance.
a0 = doc.records[st0.entries[0]].fields.amount
a3 = doc.records[st0.entries[3]].fields.amount
check("a camt location's kind is xml", a0.location.kind, "xml")
check("and it carries a path", a0.location.path, "BkToCstmrStmt/Stmt/Ntry/Amt")
check("and WHICH occurrence, without which the path names 5 elements",
      a0.location.occurrence, 0)
check("a later entry has the same path", a3.location.path, a0.location.path)
check("and a different occurrence, which is the whole point",
      a3.location.occurrence, 3)
' THE RENDERING NOW CARRIES THE LINE, because the location does. Until
' 2026-09-20 `xml.parse` gave a node no position and this adapter's header said
' so; the renderer showed a path and an occurrence and nothing else.
check("and it renders, now naming the line", finio.describe_location(a3.location),
      "BkToCstmrStmt/Stmt/Ntry/Amt[3] (line 71)")
' THE RECORD CARRIES ITS OWN PATH AND OCCURRENCE TOO, and this is asserted
' because a perturbation that dropped it went UNCAUGHT: every check above reads
' a FIELD's location, and the record's is a second place the same fact lives.
check("the record names its own path", doc.records[st0.entries[3]].path,
      "BkToCstmrStmt/Stmt/Ntry")
check("and its own occurrence", doc.records[st0.entries[3]].occurrence, 3)
check("and the four entries before it are four different occurrences",
      (string(doc.records[st0.entries[0]].occurrence) + string(doc.records[st0.entries[1]].occurrence)
       + string(doc.records[st0.entries[2]].occurrence) + string(doc.records[st0.entries[3]].occurrence)),
      "0123")
' CONTROL: a fixed-width location is a different kind and renders differently,
' so "locations have a kind" is a distinction and not a constant.
ndoc = finio.read_file(both, ndir + "payroll.ach", {})
nlay = finio_nacha.layout_entry_detail()
nloc = finio.source_value(ndoc.source, nlay, 2, "amount").location
check("CONTROL: a NACHA location's kind is fixed_width", nloc.kind, "fixed_width")
check("and it renders as a byte range",
      contains(finio.describe_location(nloc), "bytes "), true)
' THIS CHECK USED TO ASSERT THE LIMITATION -- "and the camt one names no bytes
' at all", `has(byte_offset) = false` -- which was true when written and is the
' gap the platform closed. A test that pins a gap keeps passing after the gap is
' shut, so it is inverted rather than deleted.
check("and the camt one NOW names bytes too", has(a0.location, "byte_offset"), true)
check("and a line", has(a0.location, "line"), true)
' THE RANGE MUST CUT THE ELEMENT OUT OF THE SOURCE. A pair of plausible integers
' proves nothing -- an off-by-one, or a parent's range on a child, is still two
' numbers. Only the text they cut says which element they belong to.
raw = slurp(cdir + "statement.xml")
check("and the range cuts that very element",
      byte_slice(raw, a0.location.byte_offset, a0.location.byte_length),
      "<Amt Ccy=\"USD\">1250.00</Amt>")
' AND THE DISTINCTION THE NACHA CONTROL EXISTS FOR SURVIVES: both kinds now
' carry bytes, so what separates them is that an XML location ALSO names a path
' and an occurrence -- because a byte range does not survive the document being
' reformatted, where a path does.
check("CONTROL: the fixed_width one names no path", has(nloc, "path"), false)

' ===========================================================================
print ""
print "-- §20: DOES THE DOCUMENT SHAPE SURVIVE THE REPRESENTATION?"
' ===========================================================================
' THE TIER THIS SECOND ADAPTER EXISTS FOR. §20 says an abstraction that
' survives only one representation is a generalized parser rather than a
' description of the problem, and the only way to ask is one walk over both. A
' 94-byte fixed-width ACH file and a namespaced hierarchical XML statement have
' nothing in common physically; what must be common is the DOCUMENT.
walked = []
for each d in [ ndoc, doc ]
    kinds = []
    for each rec in d.records
        if not contains(kinds, rec.kind) then
            append(kinds, rec.kind)
        end if
    end for
    append(walked, { adapter: d.adapter,
                     has_records: count(d.records) > 0,
                     has_entities: type(d.entities) = "record",
                     has_reasons: count(d.reasons) > 0,
                     retains_source: byte_count(finio.source_text(d.source)) > 0,
                     every_record_has_kind: count(kinds) > 0,
                     declares_fidelity: type(d.byte_fidelity) = "boolean" })
end for
check("both documents carry records", walked[0].has_records and walked[1].has_records, true)
check("both carry entities", walked[0].has_entities and walked[1].has_entities, true)
check("both say how they were resolved", walked[0].has_reasons and walked[1].has_reasons, true)
check("both RETAIN THE SOURCE (Axiom 1)",
      walked[0].retains_source and walked[1].retains_source, true)
check("both declare a round-trip guarantee (§17)",
      walked[0].declares_fidelity and walked[1].declares_fidelity, true)
check("and they are two different adapters, so this is not one thing twice",
      walked[0].adapter != walked[1].adapter, true)
' AND THE PART THAT DID NOT SURVIVE, asserted rather than glossed: a
' fixed-width record carries `raw` and a byte range because it HAS them; an XML
' element does not, and a re-serialization put there under the name `raw` would
' be a lie about where those bytes came from. THE UNIFORM PART IS THE FIELD AND
' ITS LOCATION; the record is representation-shaped, which is true.
check("a NACHA record carries its raw bytes", has(ndoc.records[2], "raw"), true)
check("a camt record does not, and does not pretend to", has(doc.records[2], "raw"), false)
check("it carries its path instead", has(doc.records[2], "path"), true)
check("but BOTH records' fields carry a status",
      ndoc.records[2].fields.amount.status = "ok" and doc.records[st0.entries[0]].fields.amount.status = "ok", true)

' ===========================================================================
print ""
print "-- §17: THE TWO FIDELITIES, ASSERTED AS A DIFFERENCE"
' ===========================================================================
' An adapter states which guarantee it can provide, and until now every adapter
' in this tree said `true`, which makes the field indistinguishable from a
' constant. Re-serializing a parsed XML document CANNOT reproduce its bytes,
' so camt says false -- and then has to earn the weaker claim instead.
check("NACHA declares byte fidelity", ndoc.byte_fidelity, true)
check("camt declares that it cannot", doc.byte_fidelity, false)
nout = finio.write_text(both, ndoc)
check("and NACHA delivers it", nout.text = slurp(ndir + "payroll.ach"), true)
cout = finio.write_text(reg, doc)
check("camt does NOT reproduce the bytes, as declared", cout.text = stmt_text, false)
check("and says so as explicit loss (Axiom 8)", count(cout.loss), 1)
check("of the kind that means less of it survived", cout.loss[0].kind, "narrowed")
' SEMANTIC FIDELITY IS THE CLAIM IT DOES MAKE, so it must be earned: reparsing
' the output yields equivalent financial meaning. Without this the declaration
' is just an excuse.
again = finio.read_text(reg, cout.text, {})
check("but reparsing the output gives the same statements",
      count(again.entities.statements), count(doc.entities.statements))
ast0 = again.entities.statements[0]
check("the same entries", count(ast0.entries), count(st0.entries))
check("the same first amount",
      string(again.records[ast0.entries[0]].fields.amount.value),
      string(doc.records[st0.entries[0]].fields.amount.value))
check("the same currency", ast0.currency, st0.currency)
check("and it still agrees with its own balances", count(finio.validate(reg, again)), 0)

' ===========================================================================
print ""
print "-- AXIOM 7 IN A HIERARCHICAL SOURCE"
' ===========================================================================
' The distinction has a shape here it does not have in a fixed-width record:
' an ABSENT ELEMENT is `unknown` -- the document said nothing, which for an
' optional element is ordinary and not a defect at all -- while a PRESENT
' element whose content cannot be what it claims is `invalid`.
detailed = doc.records[st0.entries[0]].fields
bare = doc.records[st0.entries[1]].fields
check("an entry carrying NtryDtls reports its counterparty",
      detailed.counterparty.value, "ORION LTD")
check("one without it answers unknown", bare.counterparty.status, "unknown")
check("and unknown, never \"\"", is_unknown(bare.counterparty.value), true)
check("while both carry an amount", bare.amount.status, "ok")
check("and the optional element's absence is NOT an issue",
      count(finio.validate(reg, doc)), 0)
' A present element that cannot be what it claims.
junk = replace(stmt_text, "<Amt Ccy=\"USD\">87.45</Amt>", "<Amt Ccy=\"USD\">eighty seven</Amt>")
jdoc = finio.read_text(reg, junk, {})
jf = jdoc.records[jdoc.entities.statements[0].entries[1]].fields.amount
check("a non-decimal amount is invalid, not unknown", jf.status, "invalid")
check("and never zero", is_unknown(jf.value), true)
check("and the token as written is kept (§18)", jf.raw, "eighty seven")
check("and validation says the statement can no longer be totalled",
      count(finio.validate(reg, jdoc)) > 0, true)

' ===========================================================================
print ""
print "-- THE CURRENCY IS AN ATTRIBUTE, AND NOT ON EVERY AMOUNT"
' ===========================================================================
' `<Amt>` carries its currency in a Ccy ATTRIBUTE and an amount without one is
' a number in no currency, which is a defect. A summary `<Sum>` carries no
' attribute at all and is not meant to: its currency is the account's, by
' definition of the element. Reading the second with the first's rule reports
' every well-formed summary as unreadable, which is what the first draft did.
check("an entry amount carries its own currency",
      doc.records[st0.entries[0]].fields.amount.currency, "USD")
sumrec = doc.records[st0.summary].fields
check("a summary total carries none of its own, and takes the account's",
      sumrec.credit_sum.currency, "USD")
check("and it is readable", sumrec.credit_sum.status, "ok")
noccy = replace(stmt_text, "<Amt Ccy=\"USD\">1250.00</Amt>", "<Amt>1250.00</Amt>")
ndoc2 = finio.read_text(reg, noccy, {})
nf = ndoc2.records[ndoc2.entities.statements[0].entries[0]].fields.amount
check("an Amt with no Ccy attribute is invalid", nf.status, "invalid")
check("and the reason says why", contains(nf.why, "no currency"), true)
' AN ENTRY IN ANOTHER CURRENCY IS REPORTED, NOT RAISED. `money` refuses to add
' different currencies -- the right guard, and why this cannot silently produce
' a nonsense total -- but a validator that let that refusal propagate would DIE
' on the malformed statement instead of describing it (§15).
mixed = finio.read_file(reg, cdir + "mixed_currency.xml", {})
mcodes = []
for each i in finio.validate(reg, mixed)
    append(mcodes, i.code)
end for
check("a foreign-currency entry is reported", contains(mcodes, "entry_currency"), true)
check("and excluded from the totals rather than added", contains(mcodes, "uncountable_entry"), true)
check("CONTROL: the well-formed statement reports neither",
      contains(mcodes, "entry_currency") and count(finio.validate(reg, doc)) = 0, true)

' ===========================================================================
print ""
print "-- TWO REAL ADAPTERS DO NOT CLAIM EACH OTHER'S FILES"
' ===========================================================================
' Ambiguity was tested with a deliberately greedy test double. This is the
' question a registry actually faces: two adapters that both work, over files
' each is not for. A cross-claim would make every mixed archive ambiguous.
ci = finio.identify(both, stmt_text)
check("the camt document is claimed by one adapter", count(ci.candidates), 1)
check("and it is camt", ci.candidates[0].adapter, "iso20022.camt053")
ni = finio.identify(both, slurp(ndir + "payroll.ach"))
check("the ACH file is claimed by one adapter", count(ni.candidates), 1)
check("and it is NACHA", ni.candidates[0].adapter, "aba.nacha")
check("neither is ambiguous",
      ci.classification != "ambiguous" and ni.classification != "ambiguous", true)

' ===========================================================================
print ""
print "-- §8: an archive holding both formats"
' ===========================================================================
report = finio.scan(both, cdir)
check("every camt fixture was examined", report.examined, 5)
' THE KEY CARRIES THE REVISION HERE AND NOT FOR NACHA, which is §8's example
' working as written for the first time: it counts "NACHA 2020 82", and only a
' format whose files declare a version can be counted that way.
check("and the recognised ones are keyed BY REVISION",
      report.counts["iso20022.camt053/camt.053.001.08"], 3)
check("including the version this adapter cannot read, counted separately",
      report.counts["iso20022.camt053/camt.053.001.02"], 1)
check("and the one that is not camt is unknown", count(report.unknown), 1)

' ===========================================================================
print ""
print "-- REFUSALS"
' ===========================================================================
on error goto next
finio.read_text(reg, slurp(cdir + "not_camt.xml"), {})
check("an unrecognised source is refused", contains(error.message, "no registered adapter"), true)
error.clear()

finio.location("xml", { path: "a/b" })
check("an xml location with no occurrence is refused",
      contains(error.message, "requires 'occurrence'"), true)
error.clear()

finio.location("xml", { path: "a/b", occurrence: 0, lien: 3 })
check("an unknown location field is refused by name",
      contains(error.message, "unknown field 'lien'"), true)
error.clear()

finio.location("elephant", { x: 1 })
check("an invented location kind is refused and the real ones named",
      contains(error.message, "fixed_width, delimited, spreadsheet, xml, json"), true)
error.clear()
on error stop
check("CONTROL: an xml location with an optional line is accepted",
      finio.location("xml", { path: "a/b", occurrence: 2, line: 47 }).line, 47)
check("and a json one needs only a pointer",
      finio.describe_location(finio.location("json", { json_pointer: "/a/0" })), "/a/0")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
