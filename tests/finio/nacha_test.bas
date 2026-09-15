' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_nacha -- the first adapter, and the first thing the Phase 1 framework
' has ever carried (docs/financial_adapters_design.md §20, §21).
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced harder here than in Phase 0:
' every defect this adapter can have produces AN ORDINARY-LOOKING PAYMENT FILE.
' Cents read as dollars is a plausible total a hundred times too small. A loan
' debit counted as a credit balances the other way and still balances. A field
' sliced one byte early returns a shorter account number that still looks like
' an account number. A golden would record any of those as expected.
'
' THE LOAD-BEARING TIER IS THE FILE'S OWN ARITHMETIC. A NACHA file states its
' own entry counts, a hash of the routing numbers it touched, and its debit and
' credit totals, in control records the producer computed. The fixtures here
' were written by tools/make_nacha_fixture.py -- a separate implementation in a
' different language -- so those numbers are an ORACLE rather than a transcript
' of what this adapter says.

load finio
load finio_nacha

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

' A SECOND ADAPTER THAT ALSO CLAIMS ACH FILES. It exists for one tier and
' could not be borrowed: ambiguity is a property of a REGISTRY, so with one
' adapter registered `identify` can never return `ambiguous` and the refusal
' that matters most -- Axiom 6, no plausible reading chosen silently -- would
' go untested.
' A `{file}` modifier is an ASSIGNMENT form, so a path cannot be turned into a
' file reference inside an expression.
function slurp(path)
    f {file}= path
    return read(f)
end function

function rival_recognise(text)
    if byte_count(text) > 0 and byte_slice(text, 0, 1) = "1" then
        return { classification: "strong", framing: "lines",
                 reasons: [ "it begins with a 1, which is all this adapter looks at" ] }
    end if
    return { classification: "unknown", reasons: [ "does not begin with 1" ] }
end function

function rival_read(src, revision)
    return { records: [], entities: {}, loss: [] }
end function

function rival_adapter()
    return finio.adapter({
        id: "test.rival",
        revisions: [ "1" ],
        recognise: rival_recognise,
        read: rival_read,
        registry_entry: { id: "test.rival", state: "discovered",
                          acquisition_class: "OPEN" },
        byte_fidelity: false })
end function

reg = finio.registry([ finio_nacha.adapter() ])
dir = "tests/finio/nacha/"

' ===========================================================================
print "-- COVERAGE: each layout accounts for all 94 bytes"
' ===========================================================================
' A DROPPED FIELD IS SILENT LOSS (Axiom 8) and it does not look like anything:
' the other fields still read correctly and the record still validates. It
' shows up here as a GAP, which is arithmetic rather than inspection. An
' OVERLAP is the other half and is what a transcription error off a published
' guide produces -- those number positions from 1, so a layout copied straight
' out of one is off by one everywhere.
for each kind in [ "file_header", "batch_header", "entry_detail", "addenda",
                   "batch_control", "file_control" ]
    cov = finio.coverage(finio_nacha.layout_for(kind), 94)
    check(kind + " covers 94 bytes with no gap and no overlap", cov.complete, true)
end for
' AND THE CONTROL, without which `coverage` could be a function that answers
' true: a layout with a field removed must report the gap it left.
short = finio.layout([ { concept: "record_type", offset: 0, length: 1 },
                       { concept: "rest", offset: 1, length: 90 } ])
gap = finio.coverage(short, 94)
check("CONTROL: a layout missing three bytes reports the gap", gap.complete, false)
check("and names where it is", string(gap.gaps[0].offset) + "+" + string(gap.gaps[0].length), "91+3")
over = finio.layout([ { concept: "a", offset: 0, length: 50 },
                      { concept: "b", offset: 40, length: 54 } ])
ov = finio.coverage(over, 94)
check("CONTROL: an overlapping layout reports the overlap", count(ov.overlaps), 1)

' ===========================================================================
print ""
print "-- RECOGNITION: deterministic evidence, named, and never a guess"
' ===========================================================================
payroll = slurp(dir + "payroll.ach")
r = finio_nacha.recognise(payroll)
check("a real ACH file is recognised exactly", r.classification, "exact")
check("and the fingerprint is named as the reason",
      contains(join(r.reasons, " | "), "record size 094"), true)
near = slurp(dir + "near_miss.dat")
rn = finio_nacha.recognise(near)
check("94-byte records that are not ACH are NOT exact", rn.classification != "exact", true)
check("they are not recognised at all", rn.classification, "unknown")
csv = slurp(dir + "not_ach.txt")
check("a CSV is not recognised", finio_nacha.recognise(csv).classification, "unknown")
check("an empty source is not recognised", finio_nacha.recognise("").classification, "unknown")
' THE STRONG CASE IS WHAT KEEPS `exact` FROM MEANING "94 bytes". A file whose
' records, sequence and types are all right but whose header declares the wrong
' record size is still an ACH file and is still readable -- it is just not
' carrying the fingerprint, and the classification says which.
damaged = replace(payroll, "A09410", "A09310")
check("a file whose header declares 093 is strong, not exact",
      finio_nacha.recognise(damaged).classification, "strong")
' THE SHARPER NEAR MISS. `near_miss.dat` is rejected on its first byte and so
' exercises nothing; this one has the right widths, a real file header and real
' ACH records, with an entry detail sitting outside any batch. It is
' `possible` -- it may well be an ACH file that was damaged, which is a
' different statement from "this is not ACH".
jumbled = slurp(dir + "out_of_order.ach")
check("a real file in an impossible order is possible, not exact",
      finio_nacha.recognise(jumbled).classification, "possible")

' ===========================================================================
print ""
print "-- THE FILE'S OWN ARITHMETIC (§15): read explains, validate judges"
' ===========================================================================
doc = finio.read_file(reg, dir + "payroll.ach", {})
check("the framework resolved the adapter from evidence", doc.adapter, "aba.nacha")
check("and recorded how", contains(join(doc.reasons, " | "), "recognised as aba.nacha"), true)
check("140 physical records", count(doc.records), 140)
check("3 batches", count(doc.entities.batches), 3)
check("6 padding records", doc.entities.padding_records, 6)
issues = finio.validate(reg, doc)
check("the file agrees with its own control records", count(issues), 0)

' RECOGNITION AND VALIDATION ASK DIFFERENT QUESTIONS (§15), and the sequence is
' where that is visible: recognition answered `possible` for the jumbled file
' and read it anyway, which is right -- a damaged ACH file is still an ACH file
' and refusing to read it destroys the only thing an operator can work from.
' Validation is what must then NAME the damage. Without the check in validate,
' that file reads, validates clean, and reports nothing about the one thing
' actually wrong with it.
jdoc2 = finio.read_text(reg, jumbled, {})
jcodes = []
for each i in finio.validate(reg, jdoc2)
    append(jcodes, i.code)
end for
check("the jumbled file READS", count(jdoc2.records) > 0, true)
check("and validation names the sequence", contains(jcodes, "record_sequence"), true)
check("and the consequence: the entry outside a batch is in no total",
      contains(jcodes, "batch_credit_total"), true)
pcodes = []
for each i in finio.validate(reg, doc)
    append(pcodes, i.code)
end for
check("CONTROL: the well-formed file reports no sequence problem",
      contains(pcodes, "record_sequence"), false)

' THE HASH WRAPS, AND THAT IS WHY THE THIRD BATCH IS 120 ENTRIES. The entry
' hash keeps only the RIGHTMOST TEN DIGITS of the sum of the routing numbers.
' With two small batches the truncation never fires, so an implementation that
' simply summed would agree with the file and the rule would be dead code the
' suite asserted nothing about. 120 entries at 99999999 sum to 11,999,999,880,
' and the file says 1999999880.
big = doc.records[132].fields
check("the third batch's declared hash is the TRUNCATED sum", big.entry_hash.value, 1999999880)
raw_sum = finio_nacha.batch_totals(doc.records, doc.entities.batches[2]).hash
check("CONTROL: the untruncated sum is a different number, and larger",
      raw_sum > 10000000000, true)
check("and truncating it gives what the file says",
      raw_sum - floor(raw_sum / 10000000000) * 10000000000, 1999999880)

' The corrupted file is the same file with ONE number changed. Every record is
' still 94 bytes, the type sequence is still valid, and recognition still says
' `exact` -- only the arithmetic betrays it, which is the whole argument for
' §15's split.
bad = finio.read_file(reg, dir + "bad_total.ach", {})
check("the corrupted file still READS", count(bad.records), 140)
check("and is still recognised exactly", bad.classification, "exact")
bad_issues = finio.validate(reg, bad)
check("validation reports exactly one disagreement", count(bad_issues), 1)
check("and names which total", bad_issues[0].code, "batch_credit_total")
check("and gives both sides", contains(bad_issues[0].message, "4347.50") and contains(bad_issues[0].message, "4337.50"), true)
check("as an error, not a warning", bad_issues[0].severity, "error")

' ===========================================================================
print ""
print "-- TWO CHECKS THAT NEVER FIRED, because `/` is float division"
' ===========================================================================
' `n - (n / 10) * 10 != 0` is ALWAYS FALSE in gBASIC: division is not integer
' division, so the product reconstructs the dividend exactly. Written that way,
' the blocking check and the blocked-framing length check both passed
' everything. Found by a probe on a deliberately damaged file, then by sweeping
' the shape -- every fixture happened to be the right length, which is why
' reading did not find it and neither did any tier.
short = slurp(dir + "payroll.ach")
short = byte_slice(short, 0, byte_count(short) - 95)     ' one padding record less
sdoc = finio.read_text(reg, short, {})
scodes = []
for each i in finio.validate(reg, sdoc)
    append(scodes, i.code)
end for
check("139 records is not a multiple of the blocking factor", count(sdoc.records), 139)
check("and validation says so", contains(scodes, "blocking"), true)
okcodes = []
for each i in finio.validate(reg, doc)
    append(okcodes, i.code)
end for
check("CONTROL: the 140-record file reports no blocking problem",
      contains(okcodes, "blocking"), false)
check("and it reports nothing at all, so that control is not vacuous",
      count(okcodes), 0)
' The same shape in recognition: a blocked source whose length is not a
' multiple of 94 cannot be cut into records, and must not be accepted as one
' that can.
blocked_raw = slurp(dir + "blocked.ach")
chopped = byte_slice(blocked_raw, 0, byte_count(blocked_raw) - 1)
check("a blocked source of the wrong length is not recognised",
      finio_nacha.recognise(chopped).classification, "unknown")
check("and the reason names the length",
      contains(join(finio_nacha.recognise(chopped).reasons, " | "), "not a multiple of 94"), true)
check("CONTROL: the intact blocked source is recognised exactly",
      finio_nacha.recognise(blocked_raw).classification, "exact")

' ===========================================================================
print ""
print "-- FRAMING IS EVIDENCE, NOT A SETTING"
' ===========================================================================
' A great many real ACH files arrive with NO record separator at all, as one
' blocked run of 94-byte records. A newline-assuming reader sees a single
' 1880-byte record and finds nothing. THE ASSERTION IS THAT THREE PHYSICALLY
' DIFFERENT FILES GIVE ONE ANSWER -- and the runner separately asserts the
' three are genuinely different bytes, or this tier is satisfied by three
' copies of the same file.
answers = []
for each name in [ "payroll.ach", "blocked.ach", "crlf.ach" ]
    d = finio.read_file(reg, dir + name, {})
    v = finio.validate(reg, d)
    t = finio_nacha.batch_totals(d.records, d.entities.batches[0])
    append(answers, (string(count(d.records)) + "/" + string(count(d.entities.batches))
                     + "/" + string(t.count) + "/" + string(t.credit) + "/" + string(count(v))))
end for
check("line-framed and blocked read the same", answers[0], answers[1])
check("and CRLF reads the same too", answers[0], answers[2])
check("and the shared answer is the right one", answers[0], "140/3/4/433750/0")
blocked = finio.read_file(reg, dir + "blocked.ach", {})
check("the blocked file is recognised as blocked", blocked.source.framing, "fixed")
check("and the line-framed one as lines", doc.source.framing, "lines")

' ===========================================================================
print ""
print "-- BYTES, NOT CODEPOINTS, IN A FIXED-WIDTH FORMAT"
' ===========================================================================
' accented.ach differs from payroll.ach by ONE CHARACTER: an E-acute inside an
' account number, which is a field the amount comes AFTER. The record is still
' 94 bytes. It is 93 codepoints. A reader slicing by codepoint takes the amount
' one place early, so it stops being all digits, the entry drops out of the
' batch total, and the file's own control record then disagrees -- with nothing
' in the output pointing at an accent.
acc = finio.read_file(reg, dir + "accented.ach", {})
alice = acc.records[2]
check("the accented record is 94 bytes", alice.byte_length, 94)
check("and 93 codepoints, which is what makes it a test", len(alice.raw), 93)
check("the amount after it still reads", alice.fields.amount.status, "ok")
check("and reads correctly", string(alice.fields.amount.value), "1250.00")
check("CONTROL: a codepoint slice of the same range gives something else",
      mid(alice.raw, 29, 10) != alice.fields.amount.raw, true)
check("so the file still agrees with its own totals",
      count(finio.validate(reg, acc)), 0)

' ===========================================================================
print ""
print "-- §18 + AXIOM 2: provenance back to the source element"
' ===========================================================================
' TWO PATHS SLICE A FIELD AND THEY MUST NOT DRIFT. The adapter reads a record
' into values; `finio.source_value` computes a LOCATION on demand -- which is
' the architecture Phase 0's measurement chose, since holding one per value
' costs 294x the file. Nothing makes them agree except that they are both
' right, so the agreement is asserted: for every field of an entry detail
' record, the bytes the adapter kept must be the bytes the location names, and
' the location must land on those bytes IN THE SOURCE TEXT rather than only in
' the record the adapter already had.
lay = finio_nacha.layout_entry_detail()
mismatched = 0
unchecked = 0
for each f in lay
    sv = finio.source_value(doc.source, lay, 2, f.concept)
    if sv.raw != doc.records[2].fields[f.concept].raw then
        mismatched = mismatched + 1
    end if
    if byte_slice(doc.source.text, sv.location.byte_offset, sv.location.byte_length) != sv.raw then
        mismatched = mismatched + 1
    end if
    unchecked = unchecked + 1
end for
check("every entry-detail field agrees between the two paths", mismatched, 0)
check("and all eleven were checked, so the loop cannot pass by running nothing",
      unchecked, 11)
' AND ON THE ACCENTED RECORD, which is the one where a codepoint slicer and a
' byte slicer part company. Without this the tier above is satisfied by two
' paths that are wrong in the same way, since payroll.ach is pure ASCII.
alay = finio_nacha.layout_entry_detail()
asv = finio.source_value(acc.source, alay, 2, "trace_number")
check("the provenance path agrees with the adapter on an accented record",
      asv.raw, acc.records[2].fields.trace_number.raw)
check("and the trace number is the full fifteen bytes", byte_count(asv.raw), 15)
check("and the location lands on them in the file",
      byte_slice(acc.source.text, asv.location.byte_offset, asv.location.byte_length), asv.raw)

' ===========================================================================
print ""
print "-- §17 BYTE FIDELITY: reconstructed, not returned"
' ===========================================================================
' THROUGH THE FRAMEWORK, not the adapter. A consumer holding a document should
' never have to name the adapter that produced it, and routing the tier through
' `finio.write_text` is what makes the dispatch load-bearing rather than a
' function nothing calls.
for each name in [ "payroll.ach", "blocked.ach", "crlf.ach" ]
    d = finio.read_file(reg, dir + name, {})
    w = finio.write_text(reg, d)
    original = slurp(dir + name)
    check(name + " re-emits byte-identically", w.text = original, true)
    check("and the output carries the guarantee the adapter declared (§17)",
          w.byte_fidelity, true)
end for
' AND THE CONTROL, because "write gives back the source" is satisfied by a
' write that returns doc.source.text and has not reconstructed anything. A
' document with one field changed must produce DIFFERENT bytes, differing in
' exactly that field's byte range and nowhere else.
edited = finio_nacha.set_field(doc, 2, "individual_name", "ALICE MERCER-HOLT     ")
we = finio.write_text(reg, edited)
original = slurp(dir + "payroll.ach")
check("an edited document writes DIFFERENT bytes", we.text != original, true)
' AND THE ORIGINAL IS UNTOUCHED. A gBASIC record is a value, so `set_field`
' cannot reach its caller's document even by accident -- but that is a property
' of the language rather than of this code, and a version that edited in place
' would pass every other check in this tier while silently changing a document
' someone else still held.
check("and the document it was built from is unchanged",
      doc.records[2].fields.individual_name.value, "ALICE MERCER")
check("and the same length", byte_count(we.text), byte_count(original))
before = byte_slice(original, 0, 94 * 2 + 2 + 54)
after = byte_slice(we.text, 0, 94 * 2 + 2 + 54)
check("everything before the edited field is untouched", before = after, true)
tail_len = byte_count(original) - (94 * 2 + 2 + 54 + 22)
check("and everything after it is too",
      byte_slice(original, 94 * 2 + 2 + 54 + 22, tail_len) = byte_slice(we.text, 94 * 2 + 2 + 54 + 22, tail_len), true)

' ===========================================================================
print ""
print "-- AXIOM 7 and §18: unknown, invalid, and the token as written"
' ===========================================================================
check("a blank optional field is unknown, not empty",
      doc.records[0].fields.reference_code.status, "unknown")
check("and its value is unknown, never \"\"",
      is_unknown(doc.records[0].fields.reference_code.value), true)
check("a present field is ok", doc.records[2].fields.individual_name.status, "ok")
check("and the RAW TOKEN is kept beside the mapped meaning (§18)",
      doc.records[2].fields.individual_name.raw, "ALICE MERCER          ")
check("while the mapped value is trimmed",
      doc.records[2].fields.individual_name.value, "ALICE MERCER")
' An amount that is not digits is INVALID -- the source said something that
' cannot be what it claims -- and is NEVER read as zero, which would understate
' a file and look exactly like a small one.
junk = replace(payroll, "0000125000EMP0001", "00001X5000EMP0001")
jdoc = finio.read_text(reg, junk, {})
check("a non-numeric amount is invalid, not zero", jdoc.records[2].fields.amount.status, "invalid")
check("and its value is unknown, never 0", is_unknown(jdoc.records[2].fields.amount.value), true)
check("and the bytes are still there", jdoc.records[2].fields.amount.raw, "00001X5000")
check("and validation says the total no longer reconciles",
      count(finio.validate(reg, jdoc)) > 0, true)

' The direction table, and the code the table does not hold.
check("22 is a credit", finio_nacha.direction_of("22"), "credit")
check("27 is a debit", finio_nacha.direction_of("27"), "debit")
' 55 IS THE ONE THAT BREAKS THE DIGIT RULE. "2, 3, 4 credit / 7, 8, 9 debit"
' holds for checking, savings and general ledger and is wrong for loans, so a
' reader that derived the direction would put loan debits on the credit side
' and every control total in a loan file would disagree with it.
check("55 is a LOAN DEBIT, which the digit rule gets wrong",
      finio_nacha.direction_of("55"), "debit")
check("an unlisted code has no direction, rather than a default",
      finio_nacha.direction_of("99"), "unknown")
odd = replace(payroll, "622021000021", "699021000021")
odoc = finio.read_text(reg, odd, {})
oiss = finio.validate(reg, odoc)
found = false
for each i in oiss
    if i.code = "unknown_transaction_code" then
        found = true
    end if
end for
check("an unknown transaction code is REPORTED", found, true)
check("and its amount is in neither total, so the control disagrees",
      count(oiss) > 1, true)

' ===========================================================================
print ""
print "-- PADDING IS NOT A FILE CONTROL RECORD"
' ===========================================================================
' Both are type 9 and they are told apart by content. Reading the padding as a
' file control gives a batch count of 999999 and a hash of nines: ordinary
' numbers, every one wrong.
check("the last real record is the file control", doc.records[133].kind, "file_control")
check("and the ones after it are padding", doc.records[134].kind, "padding")
check("the file control is the one the entities point at", doc.entities.control, 133)

' ===========================================================================
print ""
print "-- AXIOM 6: ambiguity is refused, not resolved"
' ===========================================================================
two = finio.registry([ finio_nacha.adapter(), rival_adapter() ])
ident = finio.identify(two, payroll)
check("two adapters claiming one source is ambiguous", ident.classification, "ambiguous")
check("and both are named", count(ident.candidates), 2)
on error goto next
finio.read_text(two, payroll, {})
check("reading it is REFUSED rather than resolved",
      contains(error.message, "Pin one with"), true)
error.clear()
on error stop
' AND THE CONTROL: pinning one resolves it, or the refusal would be
' indistinguishable from an ambiguous registry being unusable.
pinned = finio.read_text(two, payroll, { adapter: "aba.nacha" })
check("CONTROL: pinning an adapter reads it", count(pinned.records), 140)
' AN ADAPTER WITH NO VALIDATOR MUST NOT ANSWER "clean". An empty issue list is
' the strongest conformance claim there is, and a caller cannot tell one made
' on no evidence from one made on a file that really conforms.
on error goto next
finio.validate(two, finio.read_text(two, payroll, { adapter: "test.rival" }))
check("an adapter with no validator is refused, not answered with []",
      contains(error.message, "declares no validator"), true)
error.clear()
on error stop
' And the same rule for writing: an adapter with no writer is refused rather
' than handed back the bytes that arrived, which would be a perfect round trip
' that serialised nothing.
on error goto next
finio.write_text(two, finio.read_text(two, payroll, { adapter: "test.rival" }))
check("an adapter with no writer is refused", contains(error.message, "declares no writer"), true)
error.clear()
on error stop
check("and says the caller pinned it",
      contains(join(pinned.reasons, " | "), "pinned adapter"), true)

' ===========================================================================
print ""
print "-- THE REVISION THE FILE DOES NOT CARRY"
' ===========================================================================
' §7's resolution diagram assumes a revision can always be settled from
' evidence. For this format it cannot: the record layout has outlived twenty
' annual rule books and nothing in a file says which produced it. The adapter
' says so rather than naming a year that would then travel in every document.
check("recognition reports no revision", is_unknown(finio_nacha.recognise(payroll).revision), true)
check("the document is read under the adapter's declared default", doc.revision, "unresolved")
check("and the reason SAYS the source carried no evidence",
      contains(join(doc.reasons, " | "), "carries no revision evidence"), true)
on error goto next
finio.read_text(reg, payroll, { revision: "2023" })
check("a revision the adapter does not implement is refused",
      contains(error.message, "does not implement revision"), true)
error.clear()
on error stop

' ===========================================================================
print ""
print "-- REFUSALS, each beside its nearest legal neighbour"
' ===========================================================================
on error goto next
finio.read_text(reg, csv, {})
check("an unrecognised source is refused by read", contains(error.message, "no registered adapter"), true)
error.clear()

finio.read_text(reg, payroll, { adaptor: "aba.nacha" })
check("a misspelled option is refused BY NAME", contains(error.message, "unknown field 'adaptor'"), true)
error.clear()

finio.find_adapter(reg, "aba.ncaha")
check("an unknown adapter names what the registry holds", contains(error.message, "it holds aba.nacha"), true)
error.clear()

finio.registry([ finio_nacha.adapter(), finio_nacha.adapter() ])
check("one adapter registered twice is refused", contains(error.message, "registered twice"), true)
error.clear()

finio.adapter({ id: "x", revisions: [ "1" ], recognise: rival_recognise,
                read: rival_read,
                registry_entry: { id: "x", state: "discovered", acquisition_class: "OPEN" } })
check("an adapter that does not state byte_fidelity is refused (§17)",
      contains(error.message, "byte_fidelity"), true)
error.clear()

finio.adapter({ id: "x", revisions: [ "1" ], recognise: "not a function",
                read: rival_read, byte_fidelity: false,
                registry_entry: { id: "x", state: "discovered", acquisition_class: "OPEN" } })
check("an adapter whose recognise is not a function is refused",
      contains(error.message, "must be a function"), true)
error.clear()

finio.adapter({ id: "x", revisions: [], recognise: rival_recognise, read: rival_read,
                byte_fidelity: false,
                registry_entry: { id: "x", state: "discovered", acquisition_class: "OPEN" } })
check("an adapter naming no revision is refused", contains(error.message, "at least one revision"), true)
error.clear()

finio_nacha.set_field(doc, 2, "individual_name", "TOO SHORT")
check("a replacement value of the wrong width is refused, never truncated",
      contains(error.message, "pad or refuse, never truncate"), true)
error.clear()

finio_nacha.set_field(doc, 2, "individal_name", "ALICE MERCER          ")
check("a misspelled concept is refused by name", contains(error.message, "no concept 'individal_name'"), true)
error.clear()

finio_nacha.set_field(doc, 134, "record_type", "9")
check("editing a padding record is refused", contains(error.message, "is padding"), true)
error.clear()

finio.open_text("abc", { format: "x", revision: "1", framing: "fixed" })
check("fixed framing with no record length is refused", contains(error.message, "requires a record_length"), true)
error.clear()

finio.open_text("abc", { format: "x", revision: "1", framing: "blocks" })
check("an invented framing is refused and the real ones named",
      contains(error.message, "not one of lines, fixed"), true)
error.clear()

finio.loss_note("rounded", { why: "x" })
check("an invented loss kind is refused and the three named",
      contains(error.message, "uninterpreted, unrepresentable, narrowed"), true)
error.clear()
on error stop
' CONTROL: the legal neighbour of every one of those.
good = finio.loss_note("narrowed", { record: 3, concept: "amount", why: "target holds whole units" })
check("CONTROL: a well-formed loss note is accepted", good.kind, "narrowed")
wide = finio.open_text("abcdef", { format: "x", revision: "1", framing: "fixed", record_length: 3 })
check("CONTROL: fixed framing with a length reads two records", count(wide.records), 2)

' ===========================================================================
print ""
print "-- §8: the archive"
' ===========================================================================
report = finio.scan(reg, dir)
check("every file in the directory was examined", report.examined, 8)
' THE KEY CARRIES NO REVISION, and that is the honest report rather than a
' missing feature. §8's example counts "NACHA 2020 82"; this adapter cannot
' tell one rule book from another from a file, so a key naming a year would be
' an invention repeated once per file.
check("six are ACH", report.counts["aba.nacha"], 6)
check("and the other two are not recognised", count(report.unknown), 2)
check("the directory holds only data, so this count cannot drift",
      count(report.files), 8)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
