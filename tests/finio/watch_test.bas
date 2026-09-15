' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE MONITORING MECHANISM (docs/financial_adapters_design.md §13 "The
' monitoring mechanism", and §9's ObservationLog).
'
' §13 said what to RECORD and what to do ONCE A REVISION IS DISCOVERED, and
' never said what a watch source is, what checking one does, what triggers a
' check or what it emits. Without those, "continually maintained" is an
' intention rather than a capability.
'
' TWO TIERS CARRY THE LOAD AND NEITHER IS A VALUE COMPARISON. `unreachable` is
' asserted as a DIFFERENCE from `unchanged`, because a watch that has stopped
' working and a format that is not moving produce the same silence and the
' whole mechanism is worthless if they are one answer. And the SIGNAL tier
' asserts §13's own claim -- that neither the calendar nor the observation log
' decides alone -- as a difference between three adapters, since a queue keyed
' on either half passes every other check here.

load finio
load finio_watch

function finio_watch_priority_refused()
    on error goto next
    finio.check_registry_entry({ id: "x", state: "discovered", acquisition_class: "OPEN",
                                 maintenance_priority: "whenever" })
    got = contains(error.message, "not one of active, periodic, dormant")
    error.clear()
    on error stop
    return got
end function

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

src = finio_watch.source("version_catalogue",
    { reference: "https://example.invalid/catalogue",
      watching: "a new camt.053 version appearing" })

print "-- THE FOUR FINDINGS ARE FOUR DIFFERENT ANSWERS"
f1 = finio_watch.check(src, { ok: true, content: "v1 v2" }, "2026-09-15")
check("a source never seen before is first_sight", f1.finding, "first_sight")
check("and becomes the baseline rather than a change", contains(f1.means, "baseline"), true)
seen = src
seen.last_seen = f1.fingerprint
check("the same content next time is unchanged",
      finio_watch.check(seen, { ok: true, content: "v1 v2" }, "2026-10-15").finding, "unchanged")
f3 = finio_watch.check(seen, { ok: true, content: "v1 v2 v3" }, "2026-11-15")
check("different content is changed", f3.finding, "changed")
' THE FINDING CARRIES WHAT THE SOURCE SAID A CHANGE WOULD MEAN, not a diff. A
' catalogue page changes when its footer year changes; `watching` is what makes
' the difference between a signal and a diff, and it travels with the finding
' because it is the sentence a person reads first.
check("and it reports what the change MEANS, not what the bytes were",
      f3.means, "a new camt.053 version appearing")
check("while carrying the previous fingerprint so the change is checkable",
      f3.was, f1.fingerprint)

print ""
print "-- UNREACHABLE IS ITS OWN ANSWER, NOT A QUIET `unchanged`"
' A source that has 403'd for six months is not a stable format -- it is a
' watch that stopped working, and reporting it as "no change" is how a
' monitoring process comes to assert the world is still by observing nothing.
' NOT HYPOTHETICAL: Nacha's own developer guide returned 403 to an automated
' fetch during the first survey, and an `unchanged` there would have been a lie
' about ACH. Asserted as a DIFFERENCE, because either answer alone is satisfied
' by a mechanism that always gives it.
f4 = finio_watch.check(seen, { ok: false, why: "HTTP 403" }, "2026-12-15")
check("a fetch that failed is unreachable", f4.finding, "unreachable")
check("and it is NOT unchanged", f4.finding != "unchanged", true)
check("it says the watch is broken rather than that the format is still",
      contains(f4.means, "says nothing about whether"), true)
check("and it keeps why", f4.why, "HTTP 403")
check("CONTROL: a fetch that succeeded on the same source is still unchanged",
      finio_watch.check(seen, { ok: true, content: "v1 v2" }, "2026-12-15").finding, "unchanged")

print ""
print "-- COUNTING IS THE POINT (§9)"
' "Preserving an unknown value is only half the benefit; counting it is what
' turns it into work." The adapters already emit loss_note("uninterpreted") on
' every read; until this library nothing accumulated them.
log = {}
i = 0
while i < 300
    log = finio_watch.observe(log, { adapter: "aba.nacha", revision: "unresolved",
                                     kind: "unknown_code", detail: "225",
                                     where: "file " + string(i),
                                     seen: "2026-09-15" })
    i = i + 1
end while
obs = finio_watch.observations_for(log, "aba.nacha")
check("three hundred sightings of one token are ONE entry", count(obs), 1)
check("with the count on it", obs[0].occurrences, 300)
' A BOUNDED SAMPLE OF WHERE (§9), not every location: an unbounded list grows
' with the traffic rather than with the finding, and the tenth example teaches
' nothing the third did not.
check("and a bounded sample of where, not three hundred paths", count(obs[0].where) <= 5, true)
check("but at least one", count(obs[0].where) >= 1, true)
log = finio_watch.observe(log, { adapter: "aba.nacha", revision: "unresolved",
                                 kind: "unknown_code", detail: "999",
                                 seen: "2026-09-16" })
check("a DIFFERENT token is a different entry",
      count(finio_watch.observations_for(log, "aba.nacha")), 2)
check("first_seen and last_seen bracket the sightings",
      obs[0].first_seen + ".." + obs[0].last_seen, "2026-09-15..2026-09-15")

print ""
print "-- PRIVACY IS ENFORCED, NOT ASKED FOR"
' §9: observations "record tokens and locations, never account numbers, party
' names, amounts, or any other content of a customer record". A guideline is
' what the first careless caller ignores; a refusal is not.
on error goto next
finio_watch.observation({ adapter: "a", revision: "r", kind: "unknown_code",
                          detail: repeat("6", 94), seen: "2026-09-15" })
check("a whole 94-byte record passed as a token is refused",
      contains(error.message, "records a TOKEN, not content"), true)
error.clear()
finio_watch.observation({ adapter: "a", revision: "r", kind: "unknown_code",
                          detail: "225", amount: "125000", seen: "2026-09-15" })
check("and a field for customer content is refused by name",
      contains(error.message, "unknown field 'amount'"), true)
error.clear()
finio_watch.observation({ adapter: "a", revision: "r", kind: "guesswork",
                          detail: "225", seen: "2026-09-15" })
check("an invented observation kind is refused and the real ones named",
      contains(error.message, "unparsed_region"), true)
error.clear()
on error stop
good = finio_watch.observation({ adapter: "a", revision: "r", kind: "unknown_code",
                                 detail: "225", where: "batch header", seen: "2026-09-15" })
check("CONTROL: a token and a location are accepted", good.detail, "225")

print ""
print "-- STALENESS IS DERIVABLE, NOT REMEMBERED (§13)"
entries = [ { id: "active.one", state: "discovered", acquisition_class: "OPEN",
              maintenance_priority: "active", next_review_due: "2026-09-01" },
            { id: "later.one", state: "discovered", acquisition_class: "OPEN",
              maintenance_priority: "periodic", next_review_due: "2027-06-01" },
            { id: "dormant.one", state: "discovered", acquisition_class: "OPEN",
              maintenance_priority: "dormant", next_review_due: "2020-01-01" },
            { id: "unscheduled.one", state: "discovered", acquisition_class: "OPEN",
              maintenance_priority: "periodic" } ]
for each e in entries
    ok = finio.check_registry_entry(e)
end for
d = finio_watch.due(entries, "2026-09-15")
ids = []
for each x in d
    append(ids, x.id)
end for
check("a format past its review date is due", contains(ids, "active.one"), true)
check("one whose date has not arrived is not", contains(ids, "later.one"), false)
' A DORMANT FORMAT IS NOT OVERDUE, however old its date. §13: "A historical
' format whose final revision is decades old may require little or no routine
' monitoring." Without this the queue fills with formats nobody expects to move
' and the ones that do are buried -- which is how a maintenance list stops
' being read.
check("a DORMANT format is never due, even six years past its date",
      contains(ids, "dormant.one"), false)
check("and one that has never been scheduled IS due, with the reason",
      contains(ids, "unscheduled.one"), true)
check("an invented maintenance priority is refused", finio_watch_priority_refused(), true)

print ""
print "-- THE SIGNAL IS THE TWO TOGETHER (§13)"
' "An adapter with old evidence and no observations may be perfectly healthy --
' a stable format simply is not moving. An adapter with recent evidence and a
' rising observation count is the interesting case, and only the two together
' say so." Asserted as an ORDERING over three adapters, because a queue keyed
' on the calendar alone or on the log alone passes every other check here.
qlog = {}
qlog = finio_watch.observe(qlog, { adapter: "active.one", revision: "r",
                                   kind: "unknown_code", detail: "A", seen: "2026-09-15" })
qlog = finio_watch.observe(qlog, { adapter: "later.one", revision: "r",
                                   kind: "unknown_field", detail: "B", seen: "2026-09-15" })
q = finio_watch.review_queue(entries, qlog, "2026-09-15")
qids = []
for each x in q
    append(qids, x.id)
end for
check("a DUE adapter with observations is in the queue", contains(qids, "active.one"), true)
check("a NOT-DUE adapter with observations is ALSO in it -- §13's interesting case",
      contains(qids, "later.one"), true)
check("a due adapter with NO observations is in it too", contains(qids, "unscheduled.one"), true)
check("and the dormant one with nothing seen is not", contains(qids, "dormant.one"), false)
' The ordering: observations outrank a bare calendar date.
' BY ID, NOT BY POSITION. Comparing "the top entry" to the bare one passes on
' a queue whose DUE half ignores the log entirely, because the NOT-due-but-
' observed entry still floats to the top from the other loop -- measured, that
' perturbation went green. The two that must be compared are the two that
' differ in exactly one thing: both are due, one has observations.
observed_and_due = unknown
due_only = unknown
for each x in q
    if x.id = "active.one" then
        observed_and_due = x
    end if
    if x.id = "unscheduled.one" then
        due_only = x
    end if
end for
check("the due adapter WITH observations counted them", observed_and_due.occurrences, 1)
check("the due adapter without them counted none", due_only.occurrences, 0)
check("and the one with observations ranks above the one without",
      observed_and_due.rank > due_only.rank, true)
check("and the queue says WHY rather than just listing",
      contains(observed_and_due.why, "could not explain"), true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
