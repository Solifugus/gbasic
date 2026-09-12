' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio PHASE 0 -- the value model (docs/financial_adapters_design.md).
'
' SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect this layer can
' have is a PLAUSIBLE RECORD. A provenance location one byte off yields an
' ordinary-looking raw field from the neighbouring column; a registry entry that
' claims `implemented` without a spec reads exactly like one that has it; a
' blank field reported as invalid reads exactly like a real defect. A golden
' would record any of them as expected and defend it.

load finio

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

' Two 29-byte records, so every offset is checkable by hand:
'   record_type 0..0, account 1..11, amount 12..19, name 20..28.
' COUNTED, not eyeballed: the first draft of these two lines did not match the
' layout beneath them and the MISMATCHes were the fixture's fault, not the
' library's -- which is the ordinary way a hand-built fixture lies.
rec1 = "6ACCT000120000012500JOE      "
rec2 = "6ACCT0002   00000000         "
text = rec1 + chr(10) + rec2 + chr(10)
src = finio.open_text(text, { format: "demo", revision: "2026" })
lay = finio.layout([ { concept: "record_type", offset: 0,  length: 1 },
                     { concept: "account",     offset: 1,  length: 11 },
                     { concept: "amount",      offset: 12, length: 8 },
                     { concept: "name",        offset: 20, length: 9 } ])

print "-- provenance is COMPUTED, and it must land on the right bytes"
sv = finio.source_value(src, lay, 0, "amount")
check("the raw bytes are the field's own", sv.raw, "00012500")
check("the byte offset is absolute in the source", sv.location.byte_offset, 12)
check("the length comes from the layout", sv.location.byte_length, 8)
check("and the record index travels with it", sv.location.record, 0)
' THE SECOND RECORD IS THE ONE THAT CATCHES AN OFF-BY-ONE IN THE RECORD BASE:
' checking only record 0 passes on an implementation that ignores the offsets
' array entirely, since record 0 begins at byte 0.
sv2 = finio.source_value(src, lay, 1, "amount")
check("the second record's offset includes its own base", sv2.location.byte_offset, 30 + 12)
' AND THE ORACLE: the bytes a location NAMES must be the bytes it RETURNED.
' Without this the two checks above are satisfied by a location that is
' self-consistently wrong.
check("the source text at that offset is the raw value",
      mid(src.text, sv2.location.byte_offset, sv2.location.byte_length), sv2.raw)
check("and for the first record too",
      mid(src.text, sv.location.byte_offset, sv.location.byte_length), sv.raw)

print ""
print "-- Axiom 7: unknown is DIFFERENT from invalid"
' Collapsing these either way loses the distinction a reviewer needs: one is a
' gap to be filled, the other a defect to be reported. Asserting only one of
' the three passes on a reader that answers it for everything.
ok_f = finio.read_field(src, lay, 0, "amount", "digits")
check("a well-formed field is ok", ok_f.status, "ok")
check("and it carries the interpreted value", ok_f.value, 12500)
blank = finio.read_field(src, lay, 1, "name", "digits")
check("a blank field is UNKNOWN, not invalid", blank.status, "unknown")
check("and its value is unknown, never zero", is_unknown(blank.value), true)
bad = finio.read_field(src, lay, 0, "name", "digits")
check("a field that cannot be what it claims is INVALID", bad.status, "invalid")
check("and the refusal says what it found", contains(bad.reason, "expected digits"), true)
' EVERY answer carries its own provenance, which is Axiom 2.
check("an unknown still names where it came from", blank.source.location.byte_offset, 30 + 20)

print ""
print "-- Axiom 6: never silently guess"
on error goto next
finio.source_value(src, lay, 0, "amonut")
check("a mistyped concept is refused BY NAME", contains(error.message, "no concept 'amonut'"), true)
check("and the message lists what there is", contains(error.message, "account"), true)
error.clear()
finio.source_value(src, lay, 9, "amount")
check("a record past the end is refused", contains(error.message, "no record 9"), true)
error.clear()
finio.layout([ { concept: "a", offset: 0, length: 2 }, { concept: "a", offset: 2, length: 2 } ])
check("a layout with a repeated concept is refused", contains(error.message, "appears twice"), true)
error.clear()
finio.layout([ { concept: "a", offset: 0, length: 0 } ])
check("a zero-length field is refused", contains(error.message, "greater than zero"), true)
error.clear()
finio.layout([ { concept: "a", offset: 0, length: 2, widht: 3 } ])
check("an unknown layout field is refused by name", contains(error.message, "unknown field 'widht'"), true)
error.clear()
finio.semantic_value("amount", 12500, [], [])
check("a semantic value with no source is refused", contains(error.message, "Axiom 2"), true)
error.clear()
on error stop
' CONTROLS -- without these the refusals above are satisfied by a library that
' refuses everything.
check("CONTROL: a good layout is accepted", count(finio.layout([ { concept: "a", offset: 0, length: 2 } ])), 1)
sem = finio.semantic_value("amount", 12500, [ sv ], [ "trim" ])
check("CONTROL: a sourced semantic value is accepted", sem.concept, "amount")
check("and sources[] is plural because the mapping is many-to-many", count(sem.sources), 1)

print ""
print "-- §9: the registry keeps a claim honest"
entry = { id: "nacha", name: "NACHA ACH", family: "ach", domain: "payments",
          authority: "Nacha", representation: "fixed_width", transport: "file",
          acquisition_class: "CONTROLLED", state: "discovered" }
check("a discovered format needs no spec", finio.check_registry_entry(entry), true)
on error goto next
claimed = entry
claimed.state = "implemented"
finio.check_registry_entry(claimed)
check("but `implemented` with no specification_sources is refused",
      contains(error.message, "names no specification_sources"), true)
error.clear()
withspec = claimed
withspec.specification_sources = [ { source_type: "purchased", reference: "Nacha Operating Rules 2021" } ]
finio.check_registry_entry(withspec)
check("and still refused without implementation_allowed (Axiom 12)",
      contains(error.message, "implementation_allowed"), true)
error.clear()
bogus = entry
bogus.state = "nearly_done"
finio.check_registry_entry(bogus)
check("an invented state is refused and the real ones are named",
      contains(error.message, "discovered, spec_obtained"), true)
error.clear()
badclass = entry
badclass.acquisition_class = "FREE"
finio.check_registry_entry(badclass)
check("an invented acquisition class is refused", contains(error.message, "HUMAN_REQUIRED"), true)
error.clear()
typo = entry
typo.authorrity = "Nacha"
finio.check_registry_entry(typo)
check("an unknown registry field is refused by name", contains(error.message, "unknown field 'authorrity'"), true)
error.clear()
on error stop
' THE CONTROL THAT MAKES THE §9 TIER MEAN SOMETHING: a fully-evidenced claim is
' ACCEPTED. Without it "the registry refuses" is satisfied by refusing every
' entry, and the five states would be decoration.
good = withspec
good.implementation_allowed = true
check("CONTROL: a fully evidenced `implemented` entry is accepted",
      finio.check_registry_entry(good), true)
check("the five states are the design's five", count(finio.registry_states()), 5)
check("and the six acquisition classes are the design's six", count(finio.acquisition_classes()), 6)

print ""
print "-- §12: adapter provenance is a DIFFERENT question from data provenance"
' Not "where did this value come from" but "why does the adapter believe this
' element has this meaning". A rule that cannot name its evidence is the thing
' this framework exists not to produce, so every link is required.
on error goto next
finio.adapter_rule({ behavior: "read amount", rule: "cols 30-39 are cents" })
check("a rule with no evidence is refused", contains(error.message, "requires 'evidence'"), true)
error.clear()
on error stop
full = finio.adapter_rule({ behavior: "read amount",
                            rule: "columns 30-39 are cents, unsigned",
                            evidence: "Nacha Operating Rules, entry detail record",
                            spec_revision: "2021",
                            authority: "Nacha",
                            retrieved: "2026-09-12" })
check("CONTROL: a full chain is accepted", full.authority, "Nacha")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
