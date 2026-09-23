' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `finio_rates` -- reference and benchmark rates from the institutions that
' publish them.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is a PLAUSIBLE RATE.
' A value read from the wrong row, a rate rounded through a double, a date off
' by one -- each yields a perfectly ordinary-looking percentage, and a golden
' would record it as expected and defend it.
'
' NO NETWORK. Every fetch replays a recorded response. A gate that reaches a
' central bank goes red when a publisher has an outage, for a reason that is not
' about gBASIC -- and it could not assert a VALUE at all, since tomorrow's SOFR
' is not today's.
load finio_rates

program main( args )
    G = { checks: 0, bad: 0 }
    o = { offline_dir: "tests/finio_rates/replay" }

    print "-- the catalogue --"
    check("four sources are published", count(finio_rates.sources()) >= 4, true)
    check("every one is keyless", _all_keyless(), true)
    s0 = finio_rates.source("nyfed_sofr")
    check("a source names its publisher", s0.publisher, "Federal Reserve Bank of New York")

    print ""
    print "-- fetching --"
    s = finio_rates.fetch("nyfed_sofr", o)
    check("the fetch succeeded", s.ok, true)
    check("and carries the publisher", s.publisher, "Federal Reserve Bank of New York")
    check("30 observations", count(s.observations), 30)

    print ""
    print "-- A RATE IS TEXT, AND THAT IS THE POINT --"
    ' THE LOAD-BEARING TIER. The two feeds disagree about representation --
    ' Treasury sends "3.788" as a STRING and the NY Fed sends 3.85 as a FLOAT --
    ' and a library that silently coerced both to a double would look correct on
    ' every value in these fixtures while losing digits on a rate quoted to more
    ' places. A basis point is 0.0001; on a hundred million of notional that is
    ' ten thousand dollars.
    t = finio_rates.fetch("treasury_avg_interest", o)
    tl = finio_rates.latest(t)
    check("Treasury's rate is kept as TEXT", type(tl.rate), "string")
    check("and the number beside it agrees", number(tl.rate) = tl.value, true)
    nl = finio_rates.latest(s)
    check("NY Fed's rate has text too", type(nl.rate), "string")
    check("and it round-trips", number(nl.rate) = nl.value, true)

    ' THE CHECK THAT ACTUALLY BITES, and the three above do not. `type(rate) =
    ' "string"` passes on a library that coerces through a double and renders
    ' the result, because string(number("3.788")) is still the string "3.788".
    ' MEASURED: of the 30 rates in this recording, exactly ONE is PADDED --
    ' 3.490, which a double renders as 3.49 -- and padding is ordinary in a
    ' published rate table. So the discriminating assertion is that a padded
    ' rate keeps ITS OWN TRAILING ZERO. Without this, perturbing the library to
    ' round-trip every value through a double produced 0 mismatches.
    padded = _find_rate(t, "3.490")
    check("a PADDED rate keeps its trailing zero", padded, "3.490")

    print ""
    print "-- REVISED IS THREE-VALUED, NOT TWO --"
    ' Axiom 7 applied: "this publisher does not report revisions" and "this value
    ' has not been revised" are DIFFERENT CLAIMS. A consumer pricing off the
    ' second deserves to know which it has. Asserted as a DIFFERENCE between the
    ' two feeds, since either alone passes on a library that answers one thing
    ' for everything.
    check("NY Fed reports revisions, so false means false", nl.revised, false)
    check("Treasury does not, so the answer is unknown", is_unknown(tl.revised), true)

    print ""
    print "-- effective on a date --"
    ' The money.rate_on rule: the latest observation ON OR BEFORE the date, so a
    ' report run for a past period sees that period's rate rather than today's.
    d = finio_rates.on_date(s, "2026-09-10")
    check("a past date resolves", is_unknown(d), false)
    check("and not to something later", d.date <= "2026-09-10", true)
    ' THE CONTROL. Without it, "resolves to something on or before" is satisfied
    ' by a function that always answers the OLDEST observation it holds.
    oldest = _oldest_date(s)
    check("CONTROL: it is not simply the oldest", d.date > oldest, true)
    ' AND A DATE BEFORE THE SERIES BEGINS IS UNKNOWN, never the oldest value --
    ' answering the oldest would be inventing a rate for a day nobody published.
    check("a date before the series is unknown",
          is_unknown(finio_rates.on_date(s, "1999-01-01")), true)

    print ""
    print "-- provenance --"
    check("an observation carries a location", has(nl, "location"), true)
    check("of kind json", nl.location.kind, "json")
    check("with a pointer into the response",
          starts_with(string(nl.location.json_pointer), "/refRates/"), true)

    print ""
    print "-- refusals --"
    on error goto next
    bad = finio_rates.fetch("nyfed_zzz", o)
    if error then
        check("an unknown source is refused BY NAME",
              contains(error.message, "unknown source 'nyfed_zzz'"), true)
        error.clear()
    else
        check("an unknown source is refused BY NAME", "accepted", "refused")
    end if

    on error goto next
    bad2 = finio_rates.fetch("nyfed_sofr", { limt: 5 })
    if error then
        check("an unknown option is refused BY NAME",
              contains(error.message, "unknown option 'limt'"), true)
        error.clear()
    else
        check("an unknown option is refused BY NAME", "accepted", "refused")
    end if

    ' A MISSING RECORDING IS AN ERROR, not an empty series. A fixture that
    ' silently answered nothing would make every tier above pass by measuring
    ' nothing.
    on error goto next
    bad3 = finio_rates.fetch("nyfed_obfr", o)
    if error then
        check("a missing recording says so", contains(error.message, "no recorded response"), true)
        error.clear()
    else
        check("a missing recording says so", "answered anyway", "refused")
    end if

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.bad)
end program

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.bad = G.bad + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
end function

function _all_keyless()
    for each s in finio_rates.sources()
        if not s.keyless then
            return false
        end if
    end for
    return true
end function

' The published text for a rate, or what was found instead -- named so a failure
' says which value arrived rather than only that one did not.
function _find_rate(series, want)
    for each ob in series.observations
        if string(ob.rate) = want then
            return string(ob.rate)
        end if
    end for
    for each ob in series.observations
        if not is_unknown(ob.value) and ob.value = number(want) then
            return "found " + string(ob.rate) + " where " + want + " was published"
        end if
    end for
    return "no observation with that value at all"
end function

function _oldest_date(series)
    o = unknown
    for each ob in series.observations
        if is_unknown(o) or ob.date < o then
            o = ob.date
        end if
    end for
    return o
end function
