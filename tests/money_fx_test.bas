' PLAT-MONEY phase 3: exchange rates, which are DATED facts.
'
' Converting without an as-of date produces a number nobody can reproduce:
' re-run last quarter's report and you silently get today's rate, and the
' figure that comes out looks perfectly defensible. That is an AUDIT problem
' rather than an arithmetic one, which is why the date is not optional.
'
' EXPECTED VALUES ARE COMPUTED INDEPENDENTLY, in exact decimal arithmetic
' outside gBASIC (python's Decimal, half-even), not recorded from a run. A
' conversion that is wrong in its last digit is exactly the kind of plausible
' answer a golden would enshrine.

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

jan {date}= "2026-01-01"
jun {date}= "2026-06-01"
mar {date}= "2026-03-01"
jul {date}= "2026-07-01"
dec {date}= "2026-12-01"
old {date}= "2025-06-01"

money.rate("USD", "EUR", "0.92", jan)
money.rate("USD", "EUR", "0.95", jun)
money.rate("USD", "JPY", "151.25", jan)
money.rate("USD", "KWD", "0.30791", jan)
money.rate("JPY", "USD", "0.0066", jan)
' Eight significant figures -- more than a double reliably holds, which is why
' a rate is given as decimal TEXT and parsed exactly.
money.rate("USD", "CHF", "0.87654321", jan)

u {USD}= "100.00"

' ------------------------------------------- the rate EFFECTIVE on a date
' Not the latest rate, and not the nearest -- the latest one whose as-of date
' is on or before the date asked for. A report run for March must see March.
check("March uses January's rate", money.convert(u, "EUR", mar), "92.00")
check("July uses June's rate", money.convert(u, "EUR", jul), "95.00")
check("December still uses June's", money.convert(u, "EUR", dec), "95.00")
check("the boundary date itself counts", money.convert(u, "EUR", jun), "95.00")

' -------------------------------------- conversion across different scales
' USD has two decimal places, JPY none, KWD three. The conversion moves
' between storage scales in one integer operation -- nothing passes through a
' double on the way.
big {USD}= "1234.56"
check("USD to EUR at the January rate", money.convert(big, "EUR", jan), "1135.80")
check("an eight-figure rate is applied exactly", money.convert(big, "CHF", jan), "1082.15")
check("USD to JPY, no minor unit", money.convert(big, "JPY", jan), "186727")
grand {USD}= "1000.00"
check("USD to KWD, three places", money.convert(grand, "KWD", jan), "307.910")
tiny {USD}= "0.01"
check("one cent converts", money.convert(tiny, "EUR", jan), "0.01")
yen {JPY}= "123"
check("JPY to USD, gaining places", money.convert(yen, "USD", jan), "0.81")

' The result really is in the target currency, not merely a number that looks
' like one -- otherwise it would silently add to the wrong thing.
converted = money.convert(u, "EUR", jan)
check("the result is money", type(converted), "money")
e {EUR}= "8.00"
check("and is in the target currency", converted + e, "100.00")

' ----------------------------------------------------- the audit question
' Knowing WHICH rate was applied is the entire point of dating them.
r = money.rate_on("USD", "EUR", jul)
check("rate_on reports the rate", r.rate, "0.95")
check("rate_on reports the date it came from", string(r.as_of), "2026-06-01")
r2 = money.rate_on("USD", "EUR", mar)
check("and reports the older rate for an older date", r2.rate, "0.92")
check("with its own as-of date", string(r2.as_of), "2026-01-01")

' Registering the same pair and date again CORRECTS it, rather than needing an
' edit -- rates get restated, and the later registration wins.
money.rate("USD", "EUR", "0.93", jan)
check("re-registering a rate corrects it", money.convert(u, "EUR", mar), "93.00")

' ------------------------------------------------------------- refusals
on error goto next

x = money.convert(u, "EUR", old)
check("a date before any rate is refused", error.message, "no USD to EUR rate known on that date")
error.clear()

' INVERSION IS REFUSED. Given USD->EUR, the EUR->USD rate is not its
' reciprocal: the two sides of a quote differ by a spread, and inverting would
' invent money. The message says a rate exists the other way so the author can
' decide rather than guess.
eur {EUR}= "50.00"
x = money.convert(eur, "USD", jun)
check("the inverse is refused, and says so", error.message, "no EUR to USD rate on that date; a USD to EUR rate exists, but inverting it would invent a spread")
error.clear()

x = money.convert(u, "GBP", jun)
check("an unknown pair is refused", error.message, "no USD to GBP rate known on that date")
error.clear()

x = money.convert(u, "EUR")
check("convert without a date is refused", error.message, "money.convert expects an amount, a currency and a date")
error.clear()

x = money.convert(u, "EUR", "2026-06-01")
check("a date-shaped STRING is not a date", error.message, "money.convert expects a date -- a rate is a dated fact")
error.clear()

money.rate("USD", "EUR", "0.9", "not a date")
check("registering with a non-date is refused", error.message, "money.rate expects an as-of date")
error.clear()

money.rate("USD", "EUR", 0.9, jan)
check("a rate given as a number is refused", error.message, "money.rate expects the rate as decimal text")
error.clear()

money.rate("USD", "EUR", "-0.9", jan)
check("a negative rate is refused", error.message, "an exchange rate must be positive")
error.clear()

money.rate("USD", "USD", "1.0", jan)
check("a self-rate is refused", error.message, "a currency needs no rate against itself")
error.clear()

money.rate("USD", "NOPE", "1.0", jan)
check("an unknown currency is refused", error.message, "no such currency in money.rate (second argument)")
error.clear()

' -------------------------------- THE CONTROL: same-currency needs no rate
' Without this, generic code converting a mixed list into one reporting
' currency would fail on the entries already in it.
same = money.convert(u, "USD", jun)
check("converting to the same currency is identity", same, "100.00")
check("and needs no registered rate", type(same), "money")
error.clear()
selfrate = money.rate_on("USD", "USD", jun)
check("rate_on for the same currency is 1", selfrate, 1)
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
