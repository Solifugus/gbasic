' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `discovery.annotate` -- what a PERSON knows and the database does not.
'
' WHY IT IS HERE AND NOT IN A CONSUMER. These are properties of the ESTATE, not
' of any one question: the same note that lets a question find
' `rpt_volume_gross` belongs in generated documentation and in a lineage walk
' that a manual ETL step would otherwise break. `nlq` had accumulated five
' options that are all this one thing, and documentation and analysis would each
' have grown their own copy.
'
' SELF-CHECKING AND FORCED, like the rest of this library: every defect here is
' a PLAUSIBLE CATALOG. A note attached to the wrong object still reads like a
' note; a supplied fact indistinguishable from a discovered one still reads like
' a catalog, and is the exact hazard §2 exists to prevent -- from the other
' direction, and worse, because a supplied fact is the only tier nobody can
' check.

load discovery

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

cat = { source: "erp",
        tables: { "erp.trading.deal": { name: "deal", type: "TABLE" },
                  "erp.trading.deal_v2_final": { name: "deal_v2_final", type: "TABLE" },
                  "erp.warehouse.fact_volume": { name: "fact_volume", type: "TABLE" } },
        columns: { "erp.trading.deal.gross_vol_mmbtu": { name: "gross_vol_mmbtu" },
                   "erp.trading.deal.contract_ref": { name: "contract_ref" } },
        primary_keys: [], edges: [] }

print "-- a note is attached, and MARKED as supplied"
c = discovery.annotate(cat, {
      "erp.trading.deal": { means: "every deal booked, whether or not it reached the warehouse",
                            authority: "live", synonyms: [ "trade" ], owner: "trading desk" },
      "erp.trading.deal_v2_final": { authority: "superseded" },
      "erp.trading.deal.gross_vol_mmbtu": { means: "gross volume before availability", unit: "MMBtu" },
      "erp.warehouse.fact_volume": { derived_from: [ "erp.trading.deal" ] },
      "": { not_modelled: [ "job", "schedule" ] } })
check("the note is there", contains(keys(c.notes), "erp.trading.deal"), true)
' §2's RULE FROM THE OTHER DIRECTION. An inferred fact must never wear the
' clothes of a declared one -- and a SUPPLIED fact is the same hazard, worse in
' one respect: it is the only tier nobody can check. Following `edges.kind`,
' which has distinguished how a fact was known since the first increment.
check("and it says it was supplied", c.notes["erp.trading.deal"].known_by, "supplied")
check("the catalog it came from is unchanged", count(keys(c.tables)), count(keys(cat.tables)))

print ""
print "-- one note kind at a time, which is the shape every consumer wants"
check("documentation walks `means`", count(keys(discovery.notes_of(c, "means"))), 2)
check("analysis asks which object is live", discovery.notes_of(c, "authority")["erp.trading.deal"], "live")
check("and which is superseded", discovery.notes_of(c, "authority")["erp.trading.deal_v2_final"], "superseded")
check("a question asks for synonyms", discovery.notes_of(c, "synonyms")["erp.trading.deal"][0], "trade")
check("lineage asks what was derived by hand", count(keys(discovery.notes_of(c, "derived_from"))), 1)
check("a unit travels with the column it describes", discovery.notes_of(c, "unit")["erp.trading.deal.gross_vol_mmbtu"], "MMBtu")
' WHICH TABLE IS LIVE IS ESTATEFORGE'S PLANTED UNDISCOVERABLE FACT -- four
' tables called customer, and nothing in any schema says which one the business
' uses. This is where that answer can finally be written down.
check("an unannotated object claims no authority", has(discovery.notes_of(c, "authority"), "erp.warehouse.fact_volume"), false)

print ""
print "-- what the estate does not hold is a fact about the ESTATE"
check("not_modelled hangs off the estate, not an object", join(discovery.not_modelled(c), ","), "job,schedule")
check("and an un-annotated catalog reports none", count(discovery.not_modelled(cat)), 0)

print ""
print "-- refusals, each beside its nearest legal neighbour"
on error goto next
discovery.annotate(cat, { "erp.nosuch.table": { means: "x" } })
' A note about something absent is a typo or a note left behind by a dropped
' table, and answering from it would describe something that is not there --
' this library's central failure, arriving through the one door that bypasses
' the database entirely.
check("a note about an absent object is refused", contains(error.message, "nothing in this catalog is called"), true)
error.clear()
discovery.annotate(cat, { "erp.trading.deal": { meaning: "x" } })
check("an unknown note kind is refused BY NAME", contains(error.message, "unknown note 'meaning'"), true)
check("and the known ones are listed", contains(error.message, "authority"), true)
error.clear()
discovery.annotate(cat, { "erp.trading.deal": { authority: "probably" } })
check("an invented authority is refused", contains(error.message, "not one of live, superseded"), true)
error.clear()
discovery.annotate(cat, { "erp.trading.deal": { synonyms: "trade" } })
check("a list given as one value is refused", contains(error.message, "it is a list"), true)
error.clear()
discovery.annotate(cat, { "erp.trading.deal": "live" })
check("a note that is not a record is refused", contains(error.message, "must be a record"), true)
error.clear()
discovery.notes_of(c, "colour")
check("an unknown kind cannot be read either", contains(error.message, "is not a note kind"), true)
error.clear()
on error stop
' CONTROLS. Without these the refusals are satisfied by a library that refuses
' everything -- and a COLUMN must be annotatable as well as a table, since the
' two are keyed differently and only one of them was checked above.
check("CONTROL: a well-formed note is accepted", count(keys(discovery.annotate(cat, { "erp.trading.deal": { owner: "x" } }).notes)), 1)
check("CONTROL: a column is annotatable too", count(keys(discovery.annotate(cat, { "erp.trading.deal.contract_ref": { unit: "id" } }).notes)), 1)
check("CONTROL: the estate key needs no object", count(keys(discovery.annotate(cat, { "": { not_modelled: [ "sla" ] } }).notes)), 1)

print ""
print "-- annotating twice replaces the note, and does not merge silently"
' A half-merged note is the worst outcome: an old `means` surviving beside a new
' `authority` reads as one coherent statement nobody made.
c2 = discovery.annotate(c, { "erp.trading.deal": { authority: "archive" } })
check("the new note wins", discovery.notes_of(c2, "authority")["erp.trading.deal"], "archive")
check("and the old field is GONE rather than half-merged", has(c2.notes["erp.trading.deal"], "means"), false)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
