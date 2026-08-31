' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' fake.bas — fabricated but realistic data (docs/fake_data_design.md).
'
' EVERY GENERATOR IS A PURE FUNCTION OF (seed, index). There is no stream
' object, because a gBASIC record is a VALUE: `s.n = s.n + 1` inside a function
' mutates a local copy and the caller never advances. Deriving each value from
' the pair instead makes generation ORDER-INDEPENDENT — row 47 is the same row
' whether you asked for it first or reached it last — which is what stops a new
' field in one generator shifting every row produced after it.
'
' The underlying RNG is gBASIC's own (SplitMix64 into xoshiro256, not libc), so
' a fixture generated here regenerates byte-for-byte anywhere.
'
' TWO LAYERS. Values below are domain-neutral; the dataset builders further
' down impose the consistency that makes data usable as INPUT rather than
' filler. Layer 1 does not know Layer 2 exists.
library fake

    load dates from "dates.bas"

    ' --- the derivation ------------------------------------------------------

    ' Distinct large odd multipliers so that (seed, index, field) triples do not
    ' collide across generators: person 3's email must not equal company 3's
    ' domain draw.
    function _at(base, i, salt)
        seed(base * 1000003 + i * 10007 + salt * 79)
        return random()
    end function

    function _int(base, i, salt, lo, hi)
        return lo + floor(_at(base, i, salt) * (hi - lo + 1))
    end function

    function _pick_at(base, i, salt, items)
        return items[_int(base, i, salt, 0, count(items) - 1)]
    end function

    ' --- Layer 1: values -----------------------------------------------------

    ' `between`, not `number`: a library function whose name matches a builtin
    ' is unreachable unqualified and warns at load, and `number` is a builtin.
    function between(base, i, lo, hi)
        return _int(base, i, 1, lo, hi)
    end function

    function pick(base, i, items)
        if type(items) != "array" or count(items) = 0 then
            error "fake.pick expects a non-empty array"
        end if
        return _pick_at(base, i, 2, items)
    end function

    ' Weighted choice. `weights` are relative and need not sum to anything.
    function weighted(base, i, items, weights)
        if count(items) != count(weights) then
            error "fake.weighted expects one weight per item"
        end if
        total = 0
        for each w in weights
            total = total + w
        next
        if total <= 0 then
            error "fake.weighted needs at least one positive weight"
        end if
        r = _at(base, i, 3) * total
        running = 0
        for k = 0 to count(items) - 1
            running = running + weights[k]
            if r < running then
                return items[k]
            end if
        end for
        return items[count(items) - 1]
    end function

    ' A standard-normal draw WITHOUT trigonometry, which gBASIC does not have:
    ' the Irwin-Hall approximation, twelve uniforms minus six. Its tails stop at
    ' six sigma, which is immaterial for fabricated business data and avoids
    ' hand-rolling a sine the way chart.bas had to.
    function _normal(base, i, salt)
        total = 0
        for k = 0 to 11
            total = total + _at(base, i, salt * 100 + k)
        end for
        return total - 6
    end function

    ' Lognormal, which is the shape real invoice and transaction values take —
    ' and the reason they satisfy Benford's law. A uniform draw does not, so a
    ' uniform amount column cannot exercise a Benford detector at all.
    function lognormal(base, i, median, sigma)
        if median <= 0 then
            error "fake.lognormal needs a positive median"
        end if
        return median * exp(_normal(base, i, 4) * sigma)
    end function

    ' --- names, and the rule that they cannot be real people -----------------
    '
    ' Assembled from parts, never drawn from a gazetteer or a real directory.
    ' Emails use RFC 2606 reserved domains and phone numbers the 555-01xx range
    ' reserved for fiction, so a generated record CANNOT collide with a real
    ' person or organisation. Fake data ends up in bug reports, screenshots and
    ' shared test databases, and a plausible record naming a real person is a
    ' privacy problem the moment it leaves the machine.

    function _given_names()
        return ["Ada", "Bea", "Cyrus", "Dara", "Emil", "Fern", "Goro", "Hana",
                "Ivo", "Juno", "Kai", "Lena", "Milo", "Nadia", "Oren", "Petra",
                "Quinn", "Rosa", "Sven", "Tara", "Uma", "Viktor", "Wren", "Yara"]
    end function

    function _family_names()
        return ["Achebe", "Bianchi", "Castillo", "Dvorak", "Eriksen", "Farrow",
                "Gagne", "Halloran", "Iqbal", "Jansen", "Kowal", "Lindqvist",
                "Mbeki", "Novak", "Okafor", "Pereira", "Quist", "Rahman",
                "Suzuki", "Tremblay", "Ueda", "Vasquez", "Whitlock", "Zhao"]
    end function

    function person(base, i)
        g = _pick_at(base, i, 11, _given_names())
        f = _pick_at(base, i, 12, _family_names())
        ' THE EMAIL DERIVES FROM THE NAME. Drawing it independently is what
        ' gives "Norma Fisher <tammy76@example.com>" -- a row that is wrong to
        ' anyone who reads it.
        local = lower(g) + "." + lower(f)
        return { given: g, family: f, name: g + " " + f,
                 email: local + "@example.com" }
    end function

    function _company_heads()
        return ["Alder", "Basalt", "Cedar", "Dovetail", "Ember", "Fathom",
                "Granite", "Harbour", "Ivory", "Juniper", "Kestrel", "Lantern",
                "Meridian", "Nimbus", "Onyx", "Pinnacle", "Quarry", "Ridge",
                "Summit", "Thistle", "Umber", "Verdant", "Willow", "Zephyr"]
    end function

    function _company_tails()
        return ["Analytics", "Bindery", "Consulting", "Dynamics", "Engineering",
                "Foundry", "Group", "Holdings", "Industries", "Logistics",
                "Manufacturing", "Partners", "Systems", "Trading", "Works"]
    end function

    function company(base, i)
        h = _pick_at(base, i, 21, _company_heads())
        t = _pick_at(base, i, 22, _company_tails())
        nm = h + " " + t
        return { name: nm, domain: lower(h) + ".example.com" }
    end function

    ' Streets are invented rather than drawn from a gazetteer, and the city and
    ' region agree with each other -- an address whose postcode belongs to a
    ' different city is exactly the intra-record inconsistency §3 is about.
    function _street_names()
        return ["Alder", "Beacon", "Cobble", "Dunlin", "Elmspar", "Fenmore",
                "Gullwing", "Hazelrig", "Ivyholt", "Jessamy", "Kirkwall",
                "Larkmead", "Marlow", "Netherby", "Ockham", "Pinfold"]
    end function

    function _street_types()
        return ["Street", "Avenue", "Lane", "Road", "Way", "Terrace"]
    end function

    function _places()
        return [{ city: "Ashford", region: "OR", zip: "97" },
                { city: "Brookvale", region: "MI", zip: "49" },
                { city: "Calderwood", region: "PA", zip: "15" },
                { city: "Draycott", region: "TX", zip: "78" },
                { city: "Eastmere", region: "NY", zip: "12" },
                { city: "Foxhollow", region: "CO", zip: "80" },
                { city: "Glenmara", region: "WA", zip: "98" },
                { city: "Hartsend", region: "GA", zip: "30" }]
    end function

    function address(base, i)
        pl = _pick_at(base, i, 41, _places())
        num = _int(base, i, 42, 1, 9899)
        st = _pick_at(base, i, 43, _street_names())
        ty = _pick_at(base, i, 44, _street_types())
        ' The postcode's first two digits follow the state, as a real one does.
        tail = _int(base, i, 45, 100, 999)
        return { line1: string(num) + " " + st + " " + ty,
                 city: pl.city, region: pl.region,
                 postcode: pl.zip + string(tail) }
    end function

    ' 555-01xx is reserved for fiction, so a generated number cannot ring
    ' anyone.
    function phone(base, i)
        return "+1 555 01" + _pad2(_int(base, i, 31, 0, 99))
    end function

    function _pad2(n)
        if n < 10 then
            return "0" + string(n)
        end if
        return string(n)
    end function

    ' --- dates ---------------------------------------------------------------

    ' A duration cannot be built from a variable directly (`n days` is a
    ' literal); `n * 1 days` is the form that works.
    function _plus_days(d, n)
        return d + (n * 1 days)
    end function

    function date_between(base, i, start, finish)
        span = dates.days_between(start, finish)
        if span < 0 then
            error "fake.date_between: the window ends before it starts"
        end if
        return _plus_days(start, _int(base, i, 41, 0, span))
    end function

    ' BUSINESS DATES SKIP WEEKENDS. A uniform date range puts two sevenths of
    ' activity on days the business was shut, which is not merely unrealistic --
    ' it breaks every business-day calculation downstream and makes an aging
    ' report wrong in a way nothing reports.
    function business_date(base, i, start, finish)
        d = date_between(base, i, start, finish)
        ' weekday is ISO: 6 = Saturday, 7 = Sunday. Walk back to Friday, which
        ' biases toward Fridays by about a day in seven and is the correct
        ' direction: an entry is dated when it was booked, not when it was due.
        if d.weekday = 6 then
            return _plus_days(d, -1)
        end if
        if d.weekday = 7 then
            return _plus_days(d, -2)
        end if
        return d
    end function

    ' --- money ---------------------------------------------------------------

    ' An amount as `money`, lognormally distributed around `median`. The
    ' currency is REQUIRED: money carries one and it cannot be guessed, the
    ' same rule the money and finance libraries already follow.
    function amount(base, i, currency_code, median, sigma)
        raw = lognormal(base, i, median, sigma)
        return _to_money(raw, currency_code)
    end function

    ' Round to whole minor units through TEXT, so the value is exact rather
    ' than a double that happens to look like money.
    function _to_money(raw, currency_code)
        cents = string(round(raw, 2))
        m {USD}= cents
        if currency_code = "USD" then
            return m
        end if
        ' Other currencies go through money.convert-free re-tagging by text,
        ' which is the only way to build one without a literal per currency.
        return _tag(cents, currency_code)
    end function

    function _tag(text, currency_code)
        if currency_code = "EUR" then
            e {EUR}= text
            return e
        end if
        if currency_code = "GBP" then
            g {GBP}= text
            return g
        end if
        if currency_code = "JPY" then
            j {JPY}= string(round(number(text), 0))
            return j
        end if
        error "fake.amount: currency " + string(currency_code) + " is not one of USD, EUR, GBP, JPY"
    end function

    ' --- Layer 2: datasets ---------------------------------------------------
    '
    ' Populations, not values. Consistency is the whole point (design §3, §7):
    ' a child references a parent that exists, dates run in the right order,
    ' and the arithmetic adds up -- which is what makes the output usable AS
    ' INPUT to `accounting` rather than only as filler.

    ' `n` customers, each regenerable from (base, index) alone.
    function customers(base, n)
        rows = []
        for i = 0 to n - 1
            c = company(base, i)
            p = person(base, i)
            append(rows, { id: "C" + _pad4(i),
                           name: c.name,
                           contact: p.name,
                           email: lower(p.given) + "@" + c.domain,
                           country: weighted(base, i, ["US", "CA", "GB"], [70, 20, 10]) })
        end for
        return rows
    end function

    function _pad4(n)
        t = string(n)
        while len(t) < 4
            t = "0" + t
        end while
        return t
    end function

    ' Invoices against those customers.
    '
    ' REFERENTIAL: the customer is picked BY INDEX, and the parent is
    ' regenerable from the same seed without being stored.
    ' TEMPORAL: the date is a business date inside the window.
    ' ARITHMETIC: the total is the SUM of the lines, never the reverse -- so it
    ' cannot disagree with them by a cent.
    function invoices(base, customer_rows, n, start, finish, currency_code)
        if count(customer_rows) = 0 then
            error "fake.invoices needs at least one customer"
        end if
        rows = []
        for i = 0 to n - 1
            who = customer_rows[_int(base, i, 51, 0, count(customer_rows) - 1)]
            when = business_date(base, i, start, finish)
            line_count = _int(base, i, 52, 1, 4)
            lines = []
            total = amount(base, i * 97, currency_code, 1, 0.01) * 0
            for k = 0 to line_count - 1
                each_amt = amount(base, i * 97 + k, currency_code, 1800, 1.1)
                append(lines, { description: "Item " + string(k + 1), amount: each_amt })
                total = total + each_amt
            end for
            append(rows, { id: "INV" + _pad4(i), customer: who.id, on_date: when,
                           lines: lines, total: total })
        end for
        return rows
    end function

end library
