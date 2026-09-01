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

    ' Round to whole minor units through TEXT so the value is exact, then hand
    ' the code to `money.of` rather than a hardcoded chain of modifiers.
    '
    ' This used to be an if-chain over four currencies, because `{USD}=` is a
    ' modifier and needs a LITERAL code -- so a generator could only offer the
    ' currencies whoever wrote the chain had thought of, four out of 178.
    ' `money.of(code, text)` was added for exactly this (2026-08-31).
    function _to_money(raw, currency_code)
        return money.of(currency_code, string(round(raw, 2)))
    end function

    ' --- Layer 2: datasets ---------------------------------------------------
    '
    ' Populations, not values. Consistency is the whole point (design §3, §7):
    ' a child references a parent that exists, dates run in the right order,
    ' and the arithmetic adds up -- which is what makes the output usable AS
    ' INPUT to `accounting` rather than only as filler.

    ' `n` customers, each regenerable from (base, index) alone.
    '
    ' UNIQUENESS IS A LAYER-2 GUARANTEE, NOT A LAYER-1 ONE, and the split is
    ' the point. A VALUE may repeat -- two real people are called Ada Novak,
    ' and `fake.person` drawing the same name twice is honest. A POPULATION of
    ' distinct entities may not: a customer list with two "Basalt Partners" and
    ' 2,000 rows sharing 555 email addresses is not realistic, it is broken,
    ' and anything keyed on email silently merges rows.
    '
    ' Measured before this was added: 2,000 customers gave 555 distinct emails
    ' and 359 distinct company names, because the pools saturate (24 heads x 15
    ' tails is 360 combinations). Widening the pools only moves the number --
    ' the birthday problem beats any pool at population scale. So the dataset
    ' builder disambiguates, which it CAN do because it owns the whole list,
    ' where a (seed, index) value generator cannot see its own siblings.
    function customers(base, n)
        rows = []
        seen_name = { }
        seen_email = { }
        for i = 0 to n - 1
            c = company(base, i)
            p = person(base, i)
            nm = _distinct(seen_name, c.name)
            seen_name[nm] = true
            local = lower(p.given) + "." + lower(p.family)
            addr = _distinct(seen_email, local + "@" + c.domain)
            seen_email[addr] = true
            append(rows, { id: "C" + _pad4(i),
                           name: nm,
                           contact: p.name,
                           email: addr,
                           country: weighted(base, i, ["US", "CA", "GB"], [70, 20, 10]) })
        end for
        return rows
    end function

    ' Append the smallest numeral that makes a value new. Real directories do
    ' exactly this -- `j.smith2@` exists because `j.smith@` was taken -- so the
    ' result stays plausible instead of becoming a serial number.
    function _distinct(seen, candidate)
        if is_unknown(seen[candidate]) then
            return candidate
        end if
        k = 2
        while k < 100000
            tried = _suffixed(candidate, k)
            if is_unknown(seen[tried]) then
                return tried
            end if
            k = k + 1
        end while
        error "fake: could not find a distinct value for " + string(candidate)
    end function

    ' A numeral goes before the `@` in an email and at the end of a name.
    function _suffixed(candidate, k)
        at = find(candidate, "@")
        if is_nothing(at) then
            return candidate + " " + string(k)
        end if
        return mid(candidate, 0, at) + string(k) + mid(candidate, at, len(candidate) - at)
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

    ' --- planted anomalies ---------------------------------------------------
    '
    ' A clean population with a KNOWN defect in a KNOWN place -- the one thing a
    ' hand-written fixture cannot give at scale. Twelve bad rows among five
    ' thousand good ones, and you know which twelve.
    '
    ' THE PLANTED ROWS CARRY NO MARKER, and that is the whole design. The report
    ' comes back SEPARATELY in `planted`; the rows themselves stay ordinary rows
    ' of the same shape as their neighbours. A marker field would be a back door
    ' a detector could read, and a detector tested against data that labels its
    ' own anomalies has not been tested at all. The same rule is why a planted
    ' duplicate gets a NEW id continuing the population's own sequence rather
    ' than the original's id with a suffix, and why `duplicate` REFUSES a
    ' population whose ids do not end in digits rather than inventing one that
    ' stands out.
    '
    '   rigged = fake.plant(ledger, { anomaly: "round_dollar", count: 12, at: 7 })
    '   ' rigged.rows    -- the population, same shape, n rows changed
    '   ' rigged.planted -- one record per anomaly: anomaly, id, index, was, now, source
    '
    ' `plant` returns a NEW array rather than mutating: inside a function
    ' `append` and `remove` act on a local copy, so a mutate-in-place API would
    ' silently do nothing to the caller's value -- the rule `accounting.post`
    ' already follows.

    function _plant_kinds()
        return ["round_dollar", "duplicate", "just_under", "sequence_gap", "weekend"]
    end function

    function _opt(spec, key, fallback)
        v = spec[key]
        if is_unknown(v) then
            return fallback
        end if
        return v
    end function

    function plant(rows, spec)
        if type(rows) != "array" or count(rows) = 0 then
            error "fake.plant expects a non-empty array of rows"
        end if
        if type(spec) != "record" then
            error "fake.plant expects a spec record"
        end if
        ' A record read with DOT notation RAISES on an absent field, while the
        ' subscript form answers `unknown` -- so every optional read here goes
        ' through `_opt`, or a missing `at:` would report "unknown record field"
        ' instead of what the caller has to do about it.
        kind = _opt(spec, "anomaly", unknown)
        if is_unknown(kind) or not contains(_plant_kinds(), kind) then
            error "fake.plant: unknown anomaly " + string(kind) + " (known: " + join(_plant_kinds(), ", ") + ")"
        end if
        how_many = _opt(spec, "count", unknown)
        if is_unknown(how_many) or type(how_many) != "number" or how_many < 1 then
            error "fake.plant: count must be a positive number"
        end if
        if how_many > count(rows) then
            error "fake.plant: asked for " + string(how_many) + " anomalies in " + string(count(rows)) + " rows"
        end if
        base = _opt(spec, "at", unknown)
        if is_unknown(base) then
            error "fake.plant needs `at:`, the seed that decides which rows are planted"
        end if
        amount_field = _opt(spec, "amount_field", "total")
        date_field = _opt(spec, "date_field", "on_date")
        id_field = _opt(spec, "id_field", "id")
        avoid = _opt(spec, "avoid", [])

        picked = _sample(base, count(rows), how_many, avoid)

        if kind = "round_dollar" then
            return _plant_amount(rows, picked, amount_field, id_field, base, kind, 0)
        else if kind = "just_under" then
            threshold = _opt(spec, "threshold", unknown)
            if is_unknown(threshold) then
                error "fake.plant: `just_under` needs `threshold:`, the limit the amounts must fall under"
            end if
            return _plant_amount(rows, picked, amount_field, id_field, base, kind, threshold)
        else if kind = "weekend" then
            return _plant_weekend(rows, picked, date_field, id_field, base)
        else if kind = "duplicate" then
            return _plant_duplicate(rows, picked, amount_field, date_field, id_field, base)
        end if
        return _plant_gap(rows, picked, id_field)
    end function

    ' Distinct row indices, in row order. Walking forward on a collision keeps
    ' this O(k) for the k << n case every caller has, and the error at the end
    ' is reachable only when `avoid` has already claimed nearly everything.
    function _sample(base, n, k, avoid)
        taken = { }
        for each a in avoid
            taken[string(a)] = true
        next
        picked = []
        for j = 0 to k - 1
            idx = _int(base, j, 61, 0, n - 1)
            tries = 0
            while not is_unknown(taken[string(idx)]) and tries <= n
                idx = mod(idx + 1, n)
                tries = tries + 1
            end while
            if not is_unknown(taken[string(idx)]) then
                error "fake.plant: not enough unclaimed rows to plant " + string(k) + " anomalies"
            end if
            taken[string(idx)] = true
            append(picked, idx)
        next
        return sort(picked)
    end function

    function _plant_report(kind, id, index, was, now, source)
        return { anomaly: kind, id: id, index: index, was: was, now: now, source: source }
    end function

    function _need_field(row, field, what)
        v = row[field]
        if is_unknown(v) then
            error "fake.plant: a row has no `" + field + "` field to " + what
        end if
        return v
    end function

    ' --- the amount anomalies ------------------------------------------------

    ' Money will not become a plain number (`number(m)` refuses it outright), so
    ' magnitude is read through the type's own lossless text form.
    function _money_number(m)
        if type(m) != "money" then
            error "fake.plant: the amount field holds " + type(m) + ", not money"
        end if
        return number(money.text(m))
    end function

    ' The step a human reaches for at a given size: tens below a hundred,
    ' hundreds below a thousand, thousands above. Rounding an invoice for
    ' 4,317.82 to 4,000 is what round-dollar clustering looks like.
    function _round_step(v)
        if v >= 1000 then
            return 1000
        else if v >= 100 then
            return 100
        end if
        return 10
    end function

    function _round_figure(v)
        grain = _round_step(v)
        r = round(v / grain, 0) * grain
        if r <= 0 then
            return grain
        end if
        return r
    end function

    ' Just under a limit: a gap of half a percent to five percent, taken off the
    ' threshold and rounded DOWN so the result is strictly below it. 4,950
    ' against a 5,000 approval limit.
    function _just_under(base, i, limit)
        gap = limit * (0.005 + _at(base, i, 62) * 0.045)
        grain = _round_step(limit) / 100
        if grain < 1 then
            grain = 1
        end if
        v = floor((limit - gap) / grain) * grain
        if v >= limit then
            v = limit - grain
        end if
        if v <= 0 then
            error "fake.plant: the threshold is too small to sit just under"
        end if
        return v
    end function

    function _plant_amount(rows, picked, amount_field, id_field, base, kind, threshold)
        out = rows
        report = []
        for j = 0 to count(picked) - 1
            idx = picked[j]
            row = out[idx]
            was = _need_field(row, amount_field, "change")
            code = money.currency(was)
            if kind = "round_dollar" then
                v = _round_figure(_money_number(was))
            else
                limit = threshold
                if type(limit) = "money" then
                    limit = _money_number(limit)
                end if
                v = _just_under(base, idx, limit)
            end if
            now = money.of(code, string(round(v, 2)))
            row[amount_field] = now
            out[idx] = row
            append(report, _plant_report(kind, row[id_field], idx, was, now, unknown))
        next
        return { rows: out, planted: report }
    end function

    ' --- the date anomaly ----------------------------------------------------

    ' An entry dated on a day the business was shut. `business_date` never
    ' produces one, which is what makes this plantable at all.
    function _plant_weekend(rows, picked, date_field, id_field, base)
        out = rows
        report = []
        for j = 0 to count(picked) - 1
            idx = picked[j]
            row = out[idx]
            was = _need_field(row, date_field, "move onto a weekend")
            target = 6 + _int(base, idx, 63, 0, 1)
            delta = target - was.weekday
            if delta <= 0 then
                delta = delta + 7
            end if
            now = _plus_days(was, delta)
            row[date_field] = now
            out[idx] = row
            append(report, _plant_report("weekend", row[id_field], idx, was, now, unknown))
        next
        return { rows: out, planted: report }
    end function

    ' --- the duplicate -------------------------------------------------------

    ' The same money to the same party, entered twice a few days apart under its
    ' own document number. What makes it findable is that the amount, the party
    ' and the lines all match -- so nothing about the copy may be weakened, and
    ' only the id and the date move.
    function _plant_duplicate(rows, picked, amount_field, date_field, id_field, base)
        out = rows
        fresh = _next_ids(rows, id_field, count(picked))
        report = []
        for j = 0 to count(picked) - 1
            idx = picked[j]
            row = out[idx]
            source = _need_field(row, id_field, "duplicate")
            copy = row
            copy[id_field] = fresh[j]
            when = row[date_field]
            if not is_unknown(when) then
                copy[date_field] = _plus_days(when, _int(base, idx, 64, 1, 9))
            end if
            append(out, copy)
            append(report, _plant_report("duplicate", fresh[j], count(out) - 1,
                                         unknown, unknown, source))
        next
        return { rows: out, planted: report }
    end function

    ' Ids that continue the population's own sequence, so a planted duplicate is
    ' not identifiable by its id. A population whose ids do not end in digits is
    ' REFUSED rather than given an id that stands out: an anomaly its detector
    ' can spot by shape is not a test of the detector.
    function _next_ids(rows, id_field, how_many)
        last = rows[count(rows) - 1][id_field]
        if is_unknown(last) or type(last) != "string" then
            error "fake.plant: `duplicate` needs a text `" + id_field + "` on every row"
        end if
        cut = len(last)
        while cut > 0 and _is_digit(mid(last, cut - 1, 1))
            cut = cut - 1
        end while
        if cut = len(last) then
            error "fake.plant: `duplicate` needs ids ending in digits to continue the sequence (got " + last + ")"
        end if
        prefix = mid(last, 0, cut)
        width = len(last) - cut
        top = 0
        for each r in rows
            v = r[id_field]
            if type(v) = "string" and len(v) > cut and mid(v, 0, cut) = prefix then
                n = number(mid(v, cut, len(v) - cut))
                if not is_unknown(n) and n > top then
                    top = n
                end if
            end if
        next
        made = []
        for j = 1 to how_many
            append(made, prefix + _pad_to(string(top + j), width))
        next
        return made
    end function

    function _is_digit(ch)
        return ch >= "0" and ch <= "9"
    end function

    function _pad_to(t, width)
        while len(t) < width
            t = "0" + t
        end while
        return t
    end function

    ' --- the sequence gap ----------------------------------------------------

    ' Rows that are simply not there. The ids around them still run in order, so
    ' nothing in the surviving data is wrong -- which is what makes a missing
    ' entry hard to see and worth planting. Removal walks BACKWARDS so the
    ' indices chosen against the original array stay valid as it shrinks.
    function _plant_gap(rows, picked, id_field)
        out = rows
        report = []
        for j = count(picked) - 1 to 0 step -1
            idx = picked[j]
            row = out[idx]
            remove(out, idx)
            append(report, _plant_report("sequence_gap", row[id_field], unknown,
                                         row, unknown, unknown))
        next
        return { rows: out, planted: _report_in_row_order(report) }
    end function

    ' Report rows in the order they stood in the original population, which is
    ' the order a caller reads them in -- the removal walk runs backwards.
    function _report_in_row_order(report)
        flipped = []
        for j = count(report) - 1 to 0 step -1
            append(flipped, report[j])
        next
        return flipped
    end function

end library
