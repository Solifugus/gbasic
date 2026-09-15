' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE FORMAT REGISTRY (docs/financial_adapters_design.md §9, §10, §14).
'
' §9 says the registry "may ultimately be as important as any individual
' adapter", and that it "should include formats even when implementation is
' presently impossible. Such entries become a research and acquisition queue."
' Until 2026-09-15 it held two entries, both inside the adapter that
' implemented them -- so it recorded only what had been done, which is the one
' thing a registry is not for.
'
' THE LOAD-BEARING TIER IS `classification`, and it is a DIFFERENCE. Every
' other check here passes on a registry in which every format is OPEN and
' nothing is blocked -- which would be a comfortable and false picture of this
' industry, and would make `acquisition_class` a constant with a type. What
' must hold is that the classification SEPARATES things: several classes
' present, some formats implementable without asking anyone, and some blocked
' with a named reason.

load finio
load finio_nacha
load finio_camt
load finio_registry

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

adapters = [ finio_nacha.adapter(), finio_camt.adapter() ]
all = finio_registry.all(adapters)

print "-- every entry is a valid registry entry"
n = 0
for each e in all
    ok = finio.check_registry_entry(e)
    if ok != true then
        n = n + 1
    end if
end for
check("all entries validate", n, 0)
check("and there are enough of them to be a registry", count(all) >= 10, true)

print ""
print "-- CLASSIFICATION IS A DISTINCTION, NOT A CONSTANT"
' A registry where everything is OPEN passes every other check in this file and
' tells a reader something false about the industry. What makes the field mean
' anything is that it separates.
byc = finio_registry.by_acquisition_class(all)
present = 0
for each c in finio.acquisition_classes()
    if count(byc[c]) > 0 then
        present = present + 1
    end if
end for
check("at least three acquisition classes are represented", present >= 3, true)
impl = finio_registry.implementable(all)
blocked = finio_registry.blocked(all)
check("some formats can be implemented without asking anyone", count(impl) > 0, true)
check("and some cannot", count(blocked) > 0, true)
check("and those two sets do not overlap",
      count(impl) + count(blocked) <= count(all), true)
' THE FINDING ITSELF, asserted so it cannot quietly stop being true: the
' formats a small business most needs are open, and the ones behind a licence
' are the interchange standards. If a later tranche reverses that, this check
' is where it shows.
check("BAI2, OFX, FIX and the ISO 20022 family are all implementable",
      contains(impl, "bai2") and contains(impl, "ofx") and contains(impl, "fix")
      and contains(impl, "iso20022.pain001"), true)
check("and the X12 transaction sets and ISO 8583 are not",
      contains(impl, "x12.835") or contains(impl, "x12.820") or contains(impl, "iso8583"), false)

print ""
print "-- §10's STOPPING POINT HAS CONTENT"
' "Format discovered. Implementation blocked. Human acquisition required." is a
' shrug unless it says what is missing. The validator refuses a blocked entry
' with no `blocked_by`; this asserts the registry actually exercises that, or
' the rule would be guarding nothing.
empty_reasons = 0
for each b in blocked
    if byte_count(b.blocked_by) < 20 then
        empty_reasons = empty_reasons + 1
    end if
end for
check("every blocked format names what is in the way", empty_reasons, 0)

print ""
print "-- EVERY CLAIM CARRIES ITS EVIDENCE AND A DATE"
' §14: "record exactly what evidence was used". A URL with no date is a claim
' about a page as it is today, and §13's maintenance story is that
' specifications move.
sourced = 0
undated = 0
for each e in all
    srcs = []
    if has(e, "specification_sources") then
        srcs = e.specification_sources
    end if
    if count(srcs) > 0 then
        sourced = sourced + 1
    end if
    for each s in srcs
        if not has(s, "date_retrieved") then
            undated = undated + 1
        end if
    end for
end for
check("no source is undated", undated, 0)
check("and most entries cite at least one", sourced >= count(all) - 2, true)

print ""
print "-- ONE COPY OF EACH FORMAT, STRUCTURALLY"
' An implemented format's entry lives with its adapter; the queue holds the
' rest. Two records of one format is where a registry starts lying, so the
' merge REFUSES rather than resolving -- and the refusal is asserted, since a
' merge that silently preferred one copy would pass every check above.
ids = []
dupes = 0
for each e in all
    if contains(ids, e.id) then
        dupes = dupes + 1
    end if
    append(ids, e.id)
end for
check("no id appears twice", dupes, 0)
check("the implemented formats are present", contains(ids, "aba.nacha") and contains(ids, "iso20022.camt053"), true)
' THE IDS, NOT THE RECORDS. `contains(queue(), "aba.nacha")` compares a RECORD
' to a STRING, which PLAT-EQ makes false always -- so written that way this
' check passes whatever the queue holds, which is the vacuous-assertion class
' this tree keeps catching. It was written that way first.
qids = []
for each q in finio_registry.queue()
    append(qids, q.id)
end for
check("the queue is not empty, so the next check is not vacuous", count(qids) > 0, true)
check("and an implemented format is NOT in it", contains(qids, "aba.nacha"), false)
check("nor the other one", contains(qids, "iso20022.camt053"), false)
' AND THE REFUSAL, proven rather than described: putting one in both must raise.
on error goto next
finio_registry.all([ finio_nacha.adapter(), finio_camt.adapter(),
                     finio.adapter({ id: "bai2", revisions: [ "1" ],
                                     recognise: finio_nacha.recognise,
                                     read: finio_nacha.read_source,
                                     byte_fidelity: false,
                                     registry_entry: { id: "bai2", state: "discovered",
                                                       acquisition_class: "OPEN" } }) ])
check("a format in the queue AND with an adapter is refused",
      contains(error.message, "two copies drift"), true)
error.clear()
on error stop

print ""
print "-- THE REGISTRY KNOWS WHAT IT DOES NOT KNOW"
' §14 names twenty-two candidate domains. Reporting the gap as a value is the
' difference between a registry that is honestly partial and one that merely
' looks short -- and asserting that it does NOT claim completeness is what
' stops a later tranche from quietly implying it does.
cov = finio_registry.coverage(all)
check("coverage reports the design's domain count", cov.design_domains, 22)
check("and reports fewer families than there are domains",
      count(cov.families_present) < cov.design_domains, true)
check("and says so in words a reader gets for free",
      contains(cov.note, "absent because nobody has looked"), true)

print ""
print "-- THE TWO IMPLEMENTED ENTRIES DO NOT OVERCLAIM"
' Both were corrected on 2026-09-15 against what was actually retrieved, and
' one was DOWNGRADED. These assert the corrections rather than the comfortable
' version of them.
for each e in all
    if e.id = "aba.nacha" then
        check("NACHA is `researched`, which a retrieved field-level guide now evidences", e.state, "researched")
        check("and it is DE_FACTO, not OPEN, because the authority's own guide was not retrieved",
              e.acquisition_class, "DE_FACTO")
        check("and its source names the check the layouts were put through",
              contains(e.specification_sources[0].note, "CHECKED AGAINST IT"), true)
    end if
    if e.id = "iso20022.camt053" then
        check("camt is `discovered`, DOWNGRADED from spec_obtained: no schema was ever downloaded",
              e.state, "discovered")
        check("while the specification really is public, which is a different claim",
              e.spec_public, true)
        check("and the registry records the versions that EXIST, not the one we read",
              count(e.known_revisions) > 1, true)
    end if
end for

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
