' fake.bas — fabricated but realistic data (docs/fake_data_design.md).
'
' SELF-CHECKING. A generator's failure mode is data that looks fine: a uniform
' amount column, an invoice naming a customer who does not exist, a total a
' cent away from its lines. All three read as ordinary data, so a golden would
' record them as expected. Every line below states what must be true.

load fake
load accounting

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

' Built by bracket assignment, not a record literal: `OR` is a keyword, and a
' literal key of that name did not survive the lookup.
function _zip_for(region)
    m = { }
    m["OR"] = "97"
    m["MI"] = "49"
    m["PA"] = "15"
    m["TX"] = "78"
    m["NY"] = "12"
    m["CO"] = "80"
    m["WA"] = "98"
    m["GA"] = "30"
    return m[region]
end function

' ------------------------------------------------ REPRODUCIBILITY
' The property everything else rests on. gBASIC's RNG is its own (xoshiro256,
' not libc), so this holds across machines and builds too.
a = fake.person(11, 5)
b = fake.person(11, 5)
check("the same (seed, index) gives the same person", a.name, b.name)
check("a different seed gives a different one", fake.person(12, 5).name != a.name, true)

' ------------------------------------------------ ORDER INDEPENDENCE
' The reason there is no stream object: row 47 must be row 47 whether it was
' asked for first or reached last, or adding a field to one generator shifts
' every row produced after it and committed fixtures rot.
first = fake.person(11, 47)
walked = fake.person(11, 0)
for i = 1 to 47
    walked = fake.person(11, i)
next
check("row 47 is the same reached in order as asked for directly", walked.name, first.name)

' ------------------------------------------------ INTRA-RECORD CONSISTENCY
' The gap a value generator leaves: Faker seeded at 0 gives
' "Norma Fisher <tammy76@example.com>". The email must belong to the name.
p = fake.person(11, 3)
check("the email derives from the name",
      p.email, lower(p.given) + "." + lower(p.family) + "@example.com")

' ------------------------------------------------ CANNOT BE A REAL PERSON
' Structural, not hoped-for: RFC 2606 reserved domains and the 555-01xx range
' reserved for fiction. Fake data reaches bug reports and shared databases.
check("emails use a reserved domain", ends_with(p.email, "@example.com"), true)
check("companies do too", ends_with(fake.company(11, 3).domain, ".example.com"), true)
check("phone numbers are in the fiction range", starts_with(fake.phone(11, 3), "+1 555 01"), true)

' ------------------------------------------------ DISTRIBUTIONS
' The half that finds bugs. A uniform column CANNOT exercise a Benford
' detector -- measured against Faker 39.0.0, whose pyint puts 11.0% in each
' leading digit where Benford expects 30.1% for 1.
counts = [0,0,0,0,0,0,0,0,0]
for i = 0 to 4999
    v = fake.lognormal(11, i, 2000, 1.2)
    d = number(mid(string(floor(v)), 0, 1))
    if d >= 1 then
        counts[d - 1] = counts[d - 1] + 1
    end if
next
ones = 100 * counts[0] / 5000
nines = 100 * counts[8] / 5000
check("lognormal leading digit 1 is near Benford's 30.1%", ones > 26 and ones < 34, true)
check("and 9 near its 4.6%", nines > 2.5 and nines < 7, true)
check("which uniform data is not: 1 would be ~11%", ones > 20, true)

' Weighted choice honours its weights.
us = 0
for i = 0 to 1999
    if fake.weighted(11, i, ["US", "GB"], [90, 10]) = "US" then
        us = us + 1
    end if
next
check("a 90/10 weighting lands near 90%", us > 1750 and us < 1850, true)

' ------------------------------------------------ TEMPORAL
start {date}= "2026-01-01"
finish {date}= "2026-03-31"
weekend = 0
for i = 0 to 999
    if fake.business_date(11, i, start, finish).weekday >= 6 then
        weekend = weekend + 1
    end if
next
check("business dates never fall on a weekend", weekend, 0)
inside = 0
for i = 0 to 999
    d = fake.date_between(11, i, start, finish)
    if d >= start and d <= finish then
        inside = inside + 1
    end if
next
check("every generated date is inside its window", inside, 1000)

' ------------------------------------------------ REFERENTIAL AND ARITHMETIC
cust = fake.customers(11, 200)
inv = fake.invoices(11, cust, 500, start, finish, "USD")
ids = []
for each c in cust
    append(ids, c.id)
next
missing = 0
mismatched = 0
for each v in inv
    if not contains(ids, v.customer) then
        missing = missing + 1
    end if
    s = v.total * 0
    for each ln in v.lines
        s = s + ln.amount
    next
    if s != v.total then
        mismatched = mismatched + 1
    end if
next
check("every invoice names a customer that exists", missing, 0)
check("every total equals the sum of its own lines, to the cent", mismatched, 0)

' ------------------------------------------------ THE REST OF THE SURFACE
' Probed by hand and found sound, which is not a gate -- so they are here.
v = fake.between(11, 0, 1, 6)
check("between stays inside its bounds", v >= 1 and v <= 6, true)
outside = 0
for i = 0 to 1999
    b = fake.between(11, i, 1, 6)
    if b < 1 or b > 6 then
        outside = outside + 1
    end if
next
check("and does so over 2000 draws", outside, 0)
check("pick returns a member of its list",
      contains(["a", "b", "c"], fake.pick(11, 0, ["a", "b", "c"])), true)

ad = fake.address(11, 0)
check("an address has all four parts",
      len(ad.line1) > 0 and len(ad.city) > 0 and len(ad.region) = 2 and len(ad.postcode) = 5, true)
' The postcode must agree with the region -- an address whose postcode belongs
' to another state is the intra-record defect one level down.
agree = 0
for i = 0 to 499
    a2 = fake.address(11, i)
    if starts_with(a2.postcode, _zip_for(a2.region)) then
        agree = agree + 1
    end if
next
check("every postcode agrees with its region", agree, 500)

' `amount` takes ANY of the 178 ISO currencies, because money.of does. It was
' four, hardcoded, until money.of existed.
for each cc in ["USD", "EUR", "JPY", "KWD", "CHF"]
    m = fake.amount(11, 0, cc, 1000, 0.8)
    check("amount in " + cc + " carries that currency", money.currency(m), cc)
next
' JPY has no minor unit and KWD has three -- the exponent must follow the
' currency, not a hardcoded two.
check("JPY has no decimal places", contains(string(fake.amount(11, 0, "JPY", 1000, 0.8)), "."), false)

' ------------------------------------------------ UNIQUENESS IS LAYER 2
' A VALUE may repeat -- real people share names, and `fake.person` drawing the
' same one twice is honest. A POPULATION of distinct entities may not: 2,000
' customers sharing 555 email addresses is not realistic, it is broken, and
' anything keyed on email silently merges rows. Measured before the fix: 555
' distinct emails and 359 distinct names out of 2,000, because 24 heads x 15
' tails saturates at 360 and the birthday problem beats any pool at scale.
many = fake.customers(11, 2000)
emails = []
names = []
ids = []
for each c in many
    append(emails, c.email)
    append(names, c.name)
    append(ids, c.id)
next
check("2000 customers have 2000 distinct emails", count(unique(emails)), 2000)
check("and 2000 distinct names", count(unique(names)), 2000)
check("and 2000 distinct ids", count(unique(ids)), 2000)

' THE CONTROL, and it is what makes the tier a statement about LAYERS rather
' than about de-duplication: the VALUE generator underneath still repeats,
' because two real people are called Ada Novak.
plain = []
for i = 0 to 999
    append(plain, fake.person(11, i).name)
next
check("the value generator underneath still repeats, by design",
      count(unique(plain)) < 1000, true)

' ------------------------------------------------ THE TEST THAT PROVES ALL FOUR
' Design §7: the output must drive `accounting` without a single refusal. An
' unbalanced entry, a phantom account or a cross-currency line would each be
' rejected where it was posted -- so a ledger that posts cleanly has
' DEMONSTRATED its consistency rather than claimed it.
books = accounting.chart([
  { code: "1100", name: "Receivables", kind: "asset" },
  { code: "4000", name: "Sales",       kind: "revenue" }
])
lg = accounting.ledger()
posted = 0
on error goto next
for each v in inv
    e = accounting.entry(books, v.on_date, v.id,
          [{ account: "1100", debit: v.total }, { account: "4000", credit: v.total }])
    if error then
        error.clear()
    else
        lg = accounting.post(lg, e)
        posted = posted + 1
    end if
next
on error stop
check("every generated invoice posts to a ledger", posted, count(inv))
bs = accounting.balance_sheet(books, lg, nothing)
check("and the accounting equation holds over all of it", bs.balanced, true)
check("receivables equal the revenue booked", bs.assets, bs.earnings)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
