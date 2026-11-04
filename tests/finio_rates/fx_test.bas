' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `finio_rates` EXCHANGE RATES: the ECB's euro reference rates, published and
' derived. Self-checking; run by tests/run_finio_rates.sh.
'
' Every expected rate below was computed OUTSIDE gBASIC, with Python's decimal
' module over the same recorded CSV, rounding a derived rate to ten significant
' figures half-even. An expectation computed by this library would only agree
' with itself.
'
' Two real recordings, both from data-api.ecb.europa.eu on 2026-11-03:
'   replay/ecb_fx.csv        2025-12-22 .. 2026-01-09, by date window: the year
'                            end's closing days, and the lev's last fix
'                            (Bulgaria joined the euro on 2026-01-01).
'   replay_lastn/ecb_fx.csv  lastNObservations=2, the request this library
'                            does NOT make: it answers RUB with March 2022 and
'                            carries four "indicative" series stopped in 2020.
load finio_rates

program main( args )
    G = { checks: 0, bad: 0 }
    o = { offline_dir: "tests/finio_rates/replay", from: "2025-12-22", to: "2026-01-09" }

    print "-- the source --"
    src = finio_rates.source("ecb_fx")
    check("the ECB is a source", src.publisher, "European Central Bank")
    check("and keyless", src.keyless, true)
    s = finio_rates.fetch("ecb_fx", o)
    check("the fetch succeeded", s.ok, true)
    check("354 published observations: 30 currencies, 12 days, the lev's last 6", count(s.observations), 354)
    check("the base is the euro", s.base, "EUR")

    print ""
    print "-- PUBLISHED: euro to a currency, as the ECB states it --"
    r = finio_rates.fx_on(s, "EUR", "USD", "2026-01-09")
    check("EUR to USD on 9 January", r.rate, "1.1642")
    check("  is published, not derived", r.derived, false)
    check("  kept as the publisher's text", type(r.rate), "string")
    check("  located in the response", r.via[0].location.kind, "delimited")
    check("  at the line it was read from", r.via[0].location.row, 343)

    print ""
    print "-- DERIVED, and labelled so --"
    r = finio_rates.fx_on(s, "USD", "EUR", "2026-01-09")
    check("USD to EUR is the inverse", r.rate, "0.8589589418")
    check("  labelled derived", r.derived, true)
    check("  and says how", r.how, "the inverse of EUR/USD")
    r = finio_rates.fx_on(s, "USD", "GBP", "2026-01-09")
    check("USD to GBP is a cross through the euro", r.rate, "0.7453186738")
    check("  derived from two published rates", count(r.via), 2)
    check("  and says how", r.how, "a cross through EUR: GBP per EUR over USD per EUR")
    r = finio_rates.fx_on(s, "USD", "JPY", "2026-01-09")
    check("ten significant figures, not a double's seventeen", r.rate, "157.636145")
    r = finio_rates.fx_on(s, "GBP", "GBP", "2026-01-09")
    check("a currency to itself is 1 and not derived", [r.rate, r.derived], ["1", false])

    print ""
    print "-- EFFECTIVE ON A DATE, and never stale --"
    r = finio_rates.fx_on(s, "USD", "GBP", "2025-12-27")
    check("a closing day takes the last fix before it", r.date, "2025-12-24")
    check("  which is the cross for that day", r.rate, "0.7405616357")
    check("  and says how old it is", r.age, 3 days)
    check("a date before the window is unknown", is_unknown(finio_rates.fx_on(s, "EUR", "USD", "2025-12-01")), true)
    r = finio_rates.fx_on(s, "EUR", "USD", "2026-01-05")
    check("CONTROL: a later date is not simply the oldest fix", r.date, "2026-01-05")
    r = finio_rates.fx_on(s, "EUR", "BGN", "2026-01-05")
    check("the lev, five days after its last fix, still answers", r.rate, "1.9558")
    check("the lev nine days after is unknown: a week is the limit",
          is_unknown(finio_rates.fx_on(s, "EUR", "BGN", "2026-01-09")), true)
    r = finio_rates.fx_on(s, "EUR", "BGN", "2026-01-09", { max_age: 14 days })
    check("  and a caller who allows two weeks gets it", r.rate, "1.9558")

    print ""
    print "-- LOADED INTO money.rate --"
    rep = finio_rates.load_fx(s, [{ from: "USD", to: "GBP" }, { from: "EUR", to: "BGN" },
                                  { from: "EUR", to: "USD" }])
    check("two pairs, twelve days each", rep.loaded, 24)
    check("the cross is reported derived", rep.pairs[0].derived, true)
    check("the published pair is not", rep.pairs[1].derived, false)
    check("the lev is skipped, with money's reason", rep.skipped,
          [{ from: "EUR", to: "BGN", why: "no such currency in money.rate (second argument)" }])
    jan9 {date}= "2026-01-09"
    hundred {USD}= "100"
    check("money.convert uses it: 100 USD in GBP on 9 January", money.convert(hundred, "GBP", jan9), {GBP}"74.53")
    check("  at the rate fx_on gives", money.rate_on("USD", "GBP", jan9).rate, "0.7453186738")
    ' THE DOCUMENTED HAZARD, pinned so it cannot be forgotten: money.convert
    ' carries a rate forward with no age limit, so a window loaded for January
    ' answers for June. fx_on is the lookup that refuses.
    jun1 {date}= "2026-06-01"
    check("money.convert carries January's rate into June (documented)",
          money.rate_on("USD", "GBP", jun1).as_of, jan9)
    check("  where fx_on refuses it", is_unknown(finio_rates.fx_on(s, "USD", "GBP", jun1)), true)

    print ""
    print "-- WHY THE WINDOW: what lastNObservations answers --"
    stale = finio_rates.fetch("ecb_fx", { offline_dir: "tests/finio_rates/replay_lastn" })
    check("the indicative series are excluded, and named", stale.excluded, ["ARS", "DZD", "MAD", "TWD"])
    check("the rouble's last fix is March 2022, and is not today's rate",
          is_unknown(finio_rates.fx_on(stale, "EUR", "RUB", "2026-11-03")), true)
    r = finio_rates.fx_on(stale, "EUR", "RUB", "2026-11-03", { max_age: 5000 days })
    check("  CONTROL: the series does hold it", r.date, "2022-03-01")

    print ""
    print "-- refusals --"
    on error goto next
    bad = finio_rates.fx_on(s, "EUR", "USD", "2026-01-09", { maxage: 3 days })
    if error then
        check("an unknown fx option is refused BY NAME", contains(error.message, "unknown fx option 'maxage'"), true)
        error.clear()
    else
        check("an unknown fx option is refused BY NAME", "accepted", "refused")
    end if

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.bad)
end program

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) and type(got) = type(want) then
        print "ok   " + label
    else
        G.bad = G.bad + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
end function
