' stdlib/market.bas — daily price history. Golden-compared by tests/run_market.sh.
'
' EVERY CHECK STATES ITS OWN EXPECTED ANSWER and prints ok or a MISMATCH naming
' both sides, because the two properties this library exists to guarantee both
' fail as PLAUSIBLE NUMBERS rather than as errors:
'
'   * a price series in the wrong order yields returns that are simply NEGATED,
'     which looks like ordinary market data;
'   * unadjusted prices across a split yield a -50% day, which looks like news.
'
' A golden alone would happily record either. So order is asserted against
' dates the fixture deliberately scrambles, and `adjusted` is asserted as the
' flag the caller has to be able to read.
'
' The fixture is committed and replayed through market.offline -- no network,
' ever, in either direction.

load market
load stats

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

from_d{date}= "2024-01-01"
to_d{date}= "2024-01-31"

fixtures = env("GBASIC_MARKET_FIXTURES")
if is_unknown(fixtures) then
    fixtures = "tests/market_fixtures"
end if

m = market.offline(market.stooq(), fixtures)
r = market.daily(m, "AAPL", from_d, to_d)

print "-- the fetch"
append(results, check("it succeeds", true, r.ok))
append(results, check("with no message", "", r.message))
append(results, check("five rows", 5, count(r.frame["date"])))

print ""
print "-- ORDER: the fixture is scrambled on purpose (05, 03, 08, 04, 02)"
' Stated as literal dates rather than "is sorted", so a sort that silently
' dropped or duplicated a row cannot pass.
append(results, check("row 0", "2024-01-02", string(r.frame["date"][0])))
append(results, check("row 1", "2024-01-03", string(r.frame["date"][1])))
append(results, check("row 2", "2024-01-04", string(r.frame["date"][2])))
append(results, check("row 3", "2024-01-05", string(r.frame["date"][3])))
append(results, check("row 4", "2024-01-08", string(r.frame["date"][4])))

' The consequence, stated directly: the first return must be NEGATIVE here.
' Reversed, every sign flips and nothing else about the data looks wrong.
rets = stats.simple_returns(r.frame["close"])
append(results, check("four returns from five prices", 4, count(rets)))
append(results, check("the first return is negative, as the prices are", true, rets[0] < 0))

print ""
print "-- the columns line up with their dates"
append(results, check("close on the first day", 185.64, r.frame["close"][0]))
append(results, check("open on the first day", 187.15, r.frame["open"][0]))
append(results, check("volume on the last day", 59144500, r.frame["volume"][4]))
append(results, check("high on the last day", 185.6, r.frame["high"][4]))

print ""
print "-- ADJUSTMENT is reported, never assumed"
append(results, check("stooq declares itself unadjusted", false, r.adjusted))
t = market.tiingo("dummy-key")
append(results, check("tiingo declares itself adjusted", true, t.adjusted))

print ""
print "-- the frame is the shape the rest of the library already wants"
' forensics indexes prices by column name; stats takes the flat close array.
append(results, check("indexable by column, as forensics does", true, is_array(r.frame["close"])))
append(results, check("max_drawdown reads it", true, stats.max_drawdown(r.frame["close"]).max_drawdown < 0))

print ""
print "-- failure is a value, not a raise"
miss = market.daily(m, "NOSUCH", from_d, to_d)
append(results, check("an unknown symbol reports ok=false", false, miss.ok))
append(results, check("and says which symbol", true, contains(miss.message, "NOSUCH")))
append(results, check("and hands back no frame", true, is_unknown(miss.frame)))

empty = market.daily(m, "EMPTY", from_d, to_d)
append(results, check("a header with no rows is a failure, not an empty frame", false, empty.ok))
append(results, check("and says so", true, contains(empty.message, "no rows")))

bad = market.daily(m, "", from_d, to_d)
append(results, check("an empty symbol is refused", false, bad.ok))

backwards = market.daily(m, "AAPL", to_d, from_d)
append(results, check("a reversed date range is refused", false, backwards.ok))

print ""
print "-- a JSON provider, where BOTH wire formats have to converge"
' Tiingo serves JSON, not CSV, and the two disagree twice over: `decode`
' preserves key case (`adjClose`) where the CSV header is lowercased, and
' `decode` returns real numbers where CSV returns strings. Both are normalised
' inside the library, and this is the tier that says so.
t = market.offline(market.tiingo("dummy-key"), fixtures)
tr = market.daily(t, "MSFT", from_d, to_d)
append(results, check("the JSON provider parses", true, tr.ok))
append(results, check("two rows", 2, count(tr.frame["date"])))
append(results, check("ISO timestamps become dates", "2024-01-02", string(tr.frame["date"][0])))
append(results, check("and are sorted ascending too", "2024-01-03", string(tr.frame["date"][1])))

' The one that makes `adjusted: true` honest. The fixture's raw close is
' 370.87 and its adjClose is 185.44 -- a 2:1 split. Taking the raw column here
' would report adjusted:true while handing back prices that halve overnight,
' which is precisely the -50% phantom day this library exists to prevent.
append(results, check("an adjusted provider uses adjClose, not close", 185.44, tr.frame["close"][0]))
append(results, check("numeric JSON values survive as numbers", 373, tr.frame["open"][0]))

print ""
print "-- a 200 that is not data, which is how providers refuse now"
' Found by running the live path on 2026-08-25: Stooq answers an
' unauthenticated client with a JavaScript proof-of-work interstitial, and it
' arrives as a perfectly successful 200. Parsed as CSV it yields nothing, and
' the message used to say "returned no rows" -- which sends the caller looking
' for a bad symbol or a bad date range. Injected here rather than fetched, so
' the tier stays offline.
function challenge_transport(mm, req)
    return { status: 200, body: "<!DOCTYPE html><html><body><noscript>This site requires JavaScript to verify your browser.</noscript></body></html>" }
end function
ch = market.with_transport(market.stooq(), challenge_transport)
cr = market.daily(ch, "AAPL", from_d, to_d)
append(results, check("a challenge page is a failure", false, cr.ok))
append(results, check("and is NOT reported as an empty result", false, contains(cr.message, "no rows")))
append(results, check("it says a web page came back", true, contains(cr.message, "web page")))
append(results, check("and names the cause", true, contains(cr.message, "anti-bot")))

function limited_transport(mm, req)
    return { status: 429, body: "Too Many Requests" }
end function
lm = market.with_transport(market.stooq(), limited_transport)
lr = market.daily(lm, "AAPL", from_d, to_d)
append(results, check("a 429 is a failure", false, lr.ok))
append(results, check("and says rate-limited, not 'status 429'", true, contains(lr.message, "rate-limited")))

print ""
print "-- the convenience form"
c = market.closes(m, "AAPL", from_d, to_d)
append(results, check("closes() gives the flat array", 5, count(c)))
append(results, check("in the same order", 185.64, c[0]))
append(results, check("and is unknown on failure", true, is_unknown(market.closes(m, "NOSUCH", from_d, to_d))))

bad_count = 0
for each verdict in results
    if not verdict then
        bad_count += 1
    end if
next verdict

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad_count)
