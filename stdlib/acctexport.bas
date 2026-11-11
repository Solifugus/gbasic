' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' acctexport -- business records exported for an outside accountant.
' See docs/accounting_export_design.md.
'
' An application holds what it sold and what it was paid; its accountant works
' in an accounting product. This turns the application's records into FILES
' the accountant can read or import, each export under an explicit TARGET
' PROFILE (there is no universal accounting CSV), with a manifest saying what
' went where.
'
'   b = acctexport.bundle({ organization: { name: "Worksplicer Inc.", rounding: "half_up" },
'                           customers: [...], invoices: [...], payments: [...],
'                           refunds: [...], fees: [...], payouts: [...] })
'   r = acctexport.render(b, "general", { batch_id: "2027-01" })
'   acctexport.save(r, out_dir)
'
' WHAT IT IS NOT. It GENERATES files: it does not import them, cannot see an
' import succeed, and calls no vendor API. Stable ids and the manifest help an
' application avoid sending a record twice; they cannot stop the receiving
' product from creating a duplicate (Intuit's IIF manual: "There is no
' preventative mechanism to stop the same IIF from being imported multiple
' times"). It decides no accounting: no account codes, tax treatments,
' currency conversions or journal entries are invented. A target that needs an
' account, a tax code or an item name gets it from the caller's mappings, and a
' missing one is refused by name.
'
' FINIO'S DISCIPLINE, NOT ITS CODE (design §2). Every export is classified
' representable, lossy or impossible before a byte is written (finio §16); a
' lossy one is refused unless the caller names each kind of loss it accepts,
' and an impossible one has no override. Every profile carries a registry entry
' with its sources and the date each was retrieved, checked by
' finio.check_registry_entry. A record type the profile does not carry is
' refused unless the caller lists it in `skip`, so nothing is left out
' silently.
'
' Money is gBASIC's exact money type throughout; amounts are written at the
' currency's minor unit and unit prices with every digit they hold.

library acctexport

load finio from "finio.bas"

' ---------------------------------------------------------------- helpers

function _version()
    return "1"
end function

function _refuse(what, why)
    error "acctexport." + what + ": " + why
end function

' A record's fields are a closed set: a misspelt `amout` is refused by name
' rather than read as a missing amount.
function _closed(rec, allowed, what)
    if not is_record(rec) then
        _refuse("bundle", what + " is a " + type(rec) + ", not a record")
    end if
    for each k in keys(rec)
        if not contains(allowed, k) then
            _refuse("bundle", what + " has a field '" + k + "'; it takes " + join(allowed, ", "))
        end if
    next
    return rec
end function

function _get(rec, field, fallback)
    if has(rec, field) then
        v = rec[field]
        if not is_unknown(v) and not is_nothing(v) then
            return v
        end if
    end if
    return fallback
end function

function _list(rec, field)
    v = _get(rec, field, [])
    if type(v) != "array" then
        _refuse("bundle", "'" + field + "' must be an array, not " + type(v))
    end if
    return v
end function

function _concat(a, b)
    out = a
    for each x in b
        append(out, x)
    next
    return out
end function

' The currency's minor-unit exponent, from gBASIC's own table (registered
' currencies included).
function _exponent(code)
    for each c in money.currencies()
        if c.code = code then
            return c.exponent
        end if
    next
    _refuse("bundle", "'" + code + "' is not a currency gBASIC knows; register it with money.register")
end function

function _zero(code)
    return money.of(code, "0")
end function

' Every digit the amount holds, trailing zeros past the minor unit removed.
function _full(m)
    s = money.text(m)
    e = _exponent(money.currency(m))
    dot = find(s, ".")
    if is_nothing(dot) then
        return s
    end if
    keep = dot + 1 + e
    n = len(s)
    while n > keep and mid(s, n - 1, 1) = "0"
        n = n - 1
    end while
    if n = dot + 1 then
        n = dot
    end if
    return left(s, n)
end function

' At the minor unit: what a file carries for an amount.
function _amount(m)
    return money.text(m, _exponent(money.currency(m)))
end function

function _at_minor(m)
    return money.of(money.currency(m), _amount(m)) = m
end function

' Exact rounding to the minor unit, by the rule the application declared. Done
' on the decimal text, so nothing passes through a double.
function _round(m, mode)
    code = money.currency(m)
    e = _exponent(code)
    s = money.text(m)
    neg = starts_with(s, "-")
    if neg then
        s = mid(s, 1, len(s) - 1)
    end if
    dot = find(s, ".")
    if is_nothing(dot) then
        return m
    end if
    whole = left(s, dot)
    frac = mid(s, dot + 1, len(s) - dot - 1)
    while len(frac) < e + 1
        frac = frac + "0"
    end while
    kept = left(frac, e)
    rest = mid(frac, e, len(frac) - e)
    first = number(left(rest, 1))
    beyond = mid(rest, 1, len(rest) - 1)
    exact_half = first = 5 and replace(beyond, "0", "") = ""
    up = first > 5 or (first = 5 and not exact_half)
    if exact_half then
        if mode = "half_up" then
            up = true
        else
            last = 0
            if e > 0 then
                last = number(right(kept, 1))
            else
                last = number(right(whole, 1))
            end if
            up = mod(last, 2) = 1
        end if
    end if
    text = whole
    if e > 0 then
        text = whole + "." + kept
    end if
    out = money.of(code, text)
    if up then
        unit = "1"
        if e > 0 then
            unit = "0." + repeat("0", e - 1) + "1"
        end if
        out = out + money.of(code, unit)
    end if
    if neg then
        out = -out
    end if
    return out
end function

function _pick(cond, yes, no)
    if cond then
        return yes
    end if
    return no
end function

function _two(n)
    if n < 10 then
        return "0" + string(n)
    end if
    return string(n)
end function

function _iso(d)
    if is_unknown(d) or is_nothing(d) then
        return ""
    end if
    return string(d.year) + "-" + _two(d.month) + "-" + _two(d.day)
end function

' The date formats the targets name: "YYYY-MM-DD", "DD/MM/YYYY", "MM/DD/YYYY"
' and "M/D/YYYY" (the IIF kit's examples).
function _date_as(d, fmt)
    if is_unknown(d) or is_nothing(d) then
        return ""
    end if
    if fmt = "YYYY-MM-DD" then
        return _iso(d)
    end if
    if fmt = "DD/MM/YYYY" then
        return _two(d.day) + "/" + _two(d.month) + "/" + string(d.year)
    end if
    if fmt = "MM/DD/YYYY" then
        return _two(d.month) + "/" + _two(d.day) + "/" + string(d.year)
    end if
    if fmt = "M/D/YYYY" then
        return string(d.month) + "/" + string(d.day) + "/" + string(d.year)
    end if
    _refuse("render", "'" + fmt + "' is not a date format this library writes: YYYY-MM-DD, DD/MM/YYYY, MM/DD/YYYY or M/D/YYYY")
end function

function _num(q)
    return string(q)
end function

function _is_money(v)
    return type(v) = "money"
end function

function _is_date(v)
    return type(v) = "datetime"
end function

function _problem(problems, kind, id, field, why)
    out = problems
    append(out, { type: kind, id: id, field: field, why: why })
    return out
end function

' ---------------------------------------------------------------- the bundle

function _fields()
    return { organization: ["name", "rounding"],
             customer: ["id", "name", "email", "address", "external"],
             address: ["line1", "line2", "city", "region", "postal_code", "country"],
             invoice: ["id", "number", "customer", "issue_date", "due_date", "currency", "lines", "total", "memo", "external"],
             credit_note: ["id", "number", "customer", "issue_date", "due_date", "currency", "lines", "total", "memo", "external", "applies_to"],
             line: ["id", "description", "quantity", "unit_price", "discount", "amount", "tax", "tax_code", "category", "item", "service_from", "service_to"],
             payment: ["id", "customer", "date", "currency", "amount", "method", "reference", "processor", "allocations", "external"],
             allocation: ["invoice", "amount"],
             refund: ["id", "customer", "date", "currency", "amount", "payment", "credit_note", "reference", "processor", "external"],
             fee: ["id", "date", "currency", "amount", "processor", "payment", "description", "external"],
             payout: ["id", "date", "currency", "amount", "processor", "reference", "payments", "refunds", "fees", "external"] }
end function

' acctexport.bundle(spec) -> a bundle: the records, every field checked
' against its record's closed set, every list present.
function bundle(spec)
    f = _fields()
    s = _closed(spec, ["organization", "customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"], "the bundle")
    if not has(s, "organization") then
        _refuse("bundle", "the bundle needs an organization { name, rounding }")
    end if
    org = _closed(s.organization, f.organization, "organization")
    out = { organization: org, customers: [], invoices: [], credit_notes: [], payments: [], refunds: [], fees: [], payouts: [] }
    for each c, i in _list(s, "customers")
        c2 = _closed(c, f.customer, "customer " + string(i + 1))
        if has(c2, "address") and is_record(c2.address) then
            a = _closed(c2.address, f.address, "customer " + string(i + 1) + "'s address")
        end if
        append(out.customers, c2)
    next
    for each kind in ["invoices", "credit_notes"]
        what = "invoice"
        if kind = "credit_notes" then
            what = "credit_note"
        end if
        got = []
        for each d, i in _list(s, kind)
            d2 = _closed(d, f[what], replace(what, "_", " ") + " " + string(i + 1))
            for each l, j in _list(d2, "lines")
                l2 = _closed(l, f.line, replace(what, "_", " ") + " " + string(i + 1) + " line " + string(j + 1))
            next
            append(got, d2)
        next
        out[kind] = got
    next
    for each p, i in _list(s, "payments")
        p2 = _closed(p, f.payment, "payment " + string(i + 1))
        for each a, j in _list(p2, "allocations")
            a2 = _closed(a, f.allocation, "payment " + string(i + 1) + " allocation " + string(j + 1))
        next
        append(out.payments, p2)
    next
    for each kind in ["refunds", "fees", "payouts"]
        what = left(kind, len(kind) - 1)
        got = []
        for each r, i in _list(s, kind)
            append(got, _closed(r, f[what], what + " " + string(i + 1)))
        next
        out[kind] = got
    next
    return out
end function

function _by_id(list)
    out = {}
    for each r in list
        if has(r, "id") and is_string(r.id) then
            out[r.id] = r
        end if
    next
    return out
end function

function _ids(list)
    out = []
    for each r in list
        append(out, string(_get(r, "id", "")))
    next
    return out
end function

' ---------------------------------------------------------------- validation

' acctexport.validate(b) -> { ok, problems: [{ type, id, field, why }] }.
' Required fields, money and currency, line arithmetic under the declared
' rounding, totals, allocations, refunds against what they return, and payouts
' against their parts. Nothing is corrected; everything wrong is listed.
function validate(b)
    p = []
    org = b.organization
    if not is_string(_get(org, "name", unknown)) then
        p = _problem(p, "organization", "", "name", "the organization needs a name")
    end if
    mode = _get(org, "rounding", "")
    if mode != "half_up" and mode != "half_even" then
        p = _problem(p, "organization", "", "rounding", "rounding must be \"half_up\" or \"half_even\": how the application rounded its line amounts (it is checked, never chosen here)")
    end if
    if mode != "half_up" and mode != "half_even" then
        mode = "half_even"
    end if
    for each kind in ["customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"]
        seen = {}
        for each r in b[kind]
            id = _get(r, "id", unknown)
            if not is_string(id) or id = "" then
                p = _problem(p, kind, "", "id", "every record needs an id as text")
            else if has(seen, id) then
                p = _problem(p, kind, id, "id", "the id '" + id + "' is used twice")
            else
                seen[id] = true
            end if
        next
    next
    customers = _by_id(b.customers)
    for each c in b.customers
        if not is_string(_get(c, "name", unknown)) or _get(c, "name", "") = "" then
            p = _problem(p, "customers", _get(c, "id", ""), "name", "a customer needs a name")
        end if
    next
    invoices = _by_id(b.invoices)
    numbers = {}
    for each kind in ["invoices", "credit_notes"]
        numbers = {}
        for each d in b[kind]
            p = _concat(p, _check_document(d, kind, customers, invoices, mode))
            n = _get(d, "number", "")
            if is_string(n) and n != "" then
                if has(numbers, n) then
                    p = _problem(p, kind, d.id, "number", "the number '" + n + "' is used twice")
                end if
                numbers[n] = true
            end if
        next
    next
    ' What is left of each invoice: its total less payments allocated to it and
    ' credit notes applied to it. Over-allocation is refused, not capped.
    left_of = {}
    for each inv in b.invoices
        if _is_money(_get(inv, "total", unknown)) then
            left_of[inv.id] = inv.total
        end if
    next
    for each cn in b.credit_notes
        target = _get(cn, "applies_to", "")
        if target != "" then
            if not has(invoices, target) then
                p = _problem(p, "credit_notes", cn.id, "applies_to", "it applies to invoice '" + target + "', which is not in the bundle")
            else if has(left_of, target) and _is_money(_get(cn, "total", unknown)) then
                inv = invoices[target]
                if _get(inv, "currency", "") != _get(cn, "currency", "") then
                    p = _problem(p, "credit_notes", cn.id, "applies_to", "it is in " + string(_get(cn, "currency", "")) + " and invoice '" + target + "' is in " + string(_get(inv, "currency", "")))
                else
                    left_of[target] = left_of[target] - cn.total
                end if
            end if
        end if
    next
    payments = _by_id(b.payments)
    for each pay in b.payments
        p = _concat(p, _check_cash(pay, "payments", customers))
        ccy = _get(pay, "currency", "")
        allocated = unknown
        for each a in _list(pay, "allocations")
            inv_id = _get(a, "invoice", "")
            amt = _get(a, "amount", unknown)
            if not has(invoices, inv_id) then
                p = _problem(p, "payments", pay.id, "allocations", "it is allocated to invoice '" + string(inv_id) + "', which is not in the bundle")
            else if not _is_money(amt) or amt <= _zero(money.currency(amt)) then
                p = _problem(p, "payments", pay.id, "allocations", "an allocation to '" + inv_id + "' needs a positive money amount")
            else if money.currency(amt) != ccy or _get(invoices[inv_id], "currency", "") != ccy then
                p = _problem(p, "payments", pay.id, "allocations", "the allocation to '" + inv_id + "' is not in the payment's and the invoice's currency")
            else
                if _get(invoices[inv_id], "customer", "") != _get(pay, "customer", "") then
                    p = _problem(p, "payments", pay.id, "allocations", "invoice '" + inv_id + "' belongs to another customer")
                end if
                if is_unknown(allocated) then
                    allocated = amt
                else
                    allocated = allocated + amt
                end if
                if has(left_of, inv_id) then
                    left_of[inv_id] = left_of[inv_id] - amt
                end if
            end if
        next
        if not is_unknown(allocated) and _is_money(_get(pay, "amount", unknown)) then
            if allocated > pay.amount then
                p = _problem(p, "payments", pay.id, "allocations", "it allocates " + _amount(allocated) + " of a " + _amount(pay.amount) + " payment")
            end if
        end if
    next
    for each id in keys(left_of)
        if left_of[id] < _zero(money.currency(left_of[id])) then
            p = _problem(p, "invoices", id, "total", "payments and credit notes applied to it exceed its total by " + _amount(-left_of[id]))
        end if
    next
    credit_notes = _by_id(b.credit_notes)
    refunded = {}
    for each r in b.refunds
        p = _concat(p, _check_cash(r, "refunds", customers))
        of_pay = _get(r, "payment", "")
        of_cn = _get(r, "credit_note", "")
        if (of_pay = "") = (of_cn = "") then
            p = _problem(p, "refunds", r.id, "payment", "a refund returns one payment or one credit note: give exactly one of `payment` and `credit_note`")
        else if of_pay != "" and not has(payments, of_pay) then
            p = _problem(p, "refunds", r.id, "payment", "it refunds payment '" + of_pay + "', which is not in the bundle")
        else if of_cn != "" and not has(credit_notes, of_cn) then
            p = _problem(p, "refunds", r.id, "credit_note", "it refunds credit note '" + of_cn + "', which is not in the bundle")
        else if of_pay != "" and _is_money(_get(r, "amount", unknown)) and _is_money(_get(payments[of_pay], "amount", unknown)) then
            if money.currency(r.amount) != money.currency(payments[of_pay].amount) then
                p = _problem(p, "refunds", r.id, "currency", "it is not in the currency of payment '" + of_pay + "'")
            else
                if has(refunded, of_pay) then
                    refunded[of_pay] = refunded[of_pay] + r.amount
                else
                    refunded[of_pay] = r.amount
                end if
                if refunded[of_pay] > payments[of_pay].amount then
                    p = _problem(p, "refunds", r.id, "amount", "refunds of payment '" + of_pay + "' exceed the payment")
                end if
            end if
        end if
    next
    for each fe in b.fees
        p = _concat(p, _check_cash(fe, "fees", unknown))
        if _get(fe, "payment", "") != "" and not has(payments, fe.payment) then
            p = _problem(p, "fees", fe.id, "payment", "it is charged on payment '" + fe.payment + "', which is not in the bundle")
        end if
    next
    p = _concat(p, _check_payouts(b, payments))
    return { ok: count(p) = 0, problems: p }
end function

' One invoice or credit note: header, lines, line arithmetic and total.
function _check_document(d, kind, customers, invoices, mode)
    p = []
    id = _get(d, "id", "")
    for each f in ["number", "currency"]
        if not is_string(_get(d, f, unknown)) or _get(d, f, "") = "" then
            p = _problem(p, kind, id, f, "it needs a " + f + " as text")
        end if
    next
    if not has(customers, _get(d, "customer", "")) then
        p = _problem(p, kind, id, "customer", "its customer '" + string(_get(d, "customer", "")) + "' is not in the bundle")
    end if
    if not _is_date(_get(d, "issue_date", unknown)) then
        p = _problem(p, kind, id, "issue_date", "it needs an issue_date as a date")
    end if
    if kind = "invoices" and not _is_date(_get(d, "due_date", unknown)) then
        p = _problem(p, kind, id, "due_date", "it needs a due_date as a date")
    end if
    if _is_date(_get(d, "due_date", unknown)) and _is_date(_get(d, "issue_date", unknown)) then
        if d.due_date < d.issue_date then
            p = _problem(p, kind, id, "due_date", "it is due before it was issued")
        end if
    end if
    ccy = _get(d, "currency", "")
    lines = _list(d, "lines")
    if count(lines) = 0 then
        p = _problem(p, kind, id, "lines", "it has no lines")
    end if
    sum = unknown
    for each l, i in lines
        where = "lines[" + string(i + 1) + "]"
        if not is_string(_get(l, "description", unknown)) or _get(l, "description", "") = "" then
            p = _problem(p, kind, id, where + ".description", "a line needs a description")
        end if
        q = _get(l, "quantity", unknown)
        if type(q) != "number" or q = 0 then
            p = _problem(p, kind, id, where + ".quantity", "a line needs a quantity, a number other than 0")
        end if
        ok_money = true
        for each f in ["unit_price", "amount", "discount", "tax"]
            v = _get(l, f, unknown)
            needed = f = "unit_price" or f = "amount"
            if is_unknown(v) then
                if needed then
                    p = _problem(p, kind, id, where + "." + f, "a line needs " + f + " as money")
                    ok_money = false
                end if
            else if not _is_money(v) then
                p = _problem(p, kind, id, where + "." + f, f + " must be money, not " + type(v))
                ok_money = false
            else if money.currency(v) != ccy then
                p = _problem(p, kind, id, where + "." + f, f + " is in " + money.currency(v) + " and the document is in " + ccy)
                ok_money = false
            else if f != "unit_price" and f != "amount" and v < _zero(ccy) then
                p = _problem(p, kind, id, where + "." + f, f + " cannot be negative")
                ok_money = false
            end if
        next
        if ok_money and type(q) = "number" and q != 0 then
            gross = l.unit_price * q
            net = gross - _get(l, "discount", _zero(ccy))
            want = _round(net, mode)
            if not _at_minor(l.amount) then
                p = _problem(p, kind, id, where + ".amount", "the amount " + _full(l.amount) + " is finer than the currency's minor unit")
            else if want != l.amount then
                p = _problem(p, kind, id, where + ".amount", ("quantity x unit_price - discount is " + _full(net) + ", which rounds " + mode
                                                            + " to " + _amount(want) + ", and the line says " + _amount(l.amount)))
            end if
            if not _at_minor(_get(l, "tax", _zero(ccy))) then
                p = _problem(p, kind, id, where + ".tax", "the tax " + _full(l.tax) + " is finer than the currency's minor unit")
            end if
            line_total = l.amount + _get(l, "tax", _zero(ccy))
            if is_unknown(sum) then
                sum = line_total
            else
                sum = sum + line_total
            end if
        end if
        sf = _get(l, "service_from", unknown)
        st = _get(l, "service_to", unknown)
        if (not is_unknown(sf) and not _is_date(sf)) or (not is_unknown(st) and not _is_date(st)) then
            p = _problem(p, kind, id, where + ".service_from", "a service period is two dates")
        else if _is_date(sf) and _is_date(st) then
            if st < sf then
                p = _problem(p, kind, id, where + ".service_to", "the service period ends before it begins")
            end if
        end if
    next
    total = _get(d, "total", unknown)
    if not _is_money(total) then
        p = _problem(p, kind, id, "total", "it needs a total as money")
    else if money.currency(total) != ccy then
        p = _problem(p, kind, id, "total", "the total is in " + money.currency(total) + " and the document is in " + ccy)
    else if not is_unknown(sum) and sum != total then
        p = _problem(p, kind, id, "total", "its lines (amounts and tax) come to " + _amount(sum) + " and its total says " + _amount(total))
    else if total < _zero(ccy) then
        ' A line may be negative (a discount line); a document may not: money
        ' owed back to a customer is a credit note, with positive amounts.
        p = _problem(p, kind, id, "total", "the total " + _amount(total) + " is below zero; money owed back is a credit note, with positive amounts")
    end if
    return p
end function

' A payment, refund or fee: a date, a currency and a positive amount in it.
function _check_cash(r, kind, customers)
    p = []
    id = _get(r, "id", "")
    if not is_unknown(customers) then
        if not has(customers, _get(r, "customer", "")) then
            p = _problem(p, kind, id, "customer", "its customer '" + string(_get(r, "customer", "")) + "' is not in the bundle")
        end if
    end if
    if not _is_date(_get(r, "date", unknown)) then
        p = _problem(p, kind, id, "date", "it needs a date")
    end if
    ccy = _get(r, "currency", "")
    amt = _get(r, "amount", unknown)
    if not is_string(ccy) or ccy = "" then
        p = _problem(p, kind, id, "currency", "it needs a currency as text")
    else if not _is_money(amt) then
        p = _problem(p, kind, id, "amount", "it needs an amount as money")
    else if money.currency(amt) != ccy then
        p = _problem(p, kind, id, "amount", "the amount is in " + money.currency(amt) + " and the record says " + ccy)
    else if amt <= _zero(ccy) then
        p = _problem(p, kind, id, "amount", "the amount must be positive; which way money moved is the record's type")
    else if not _at_minor(amt) then
        p = _problem(p, kind, id, "amount", "the amount " + _full(amt) + " is finer than the currency's minor unit")
    end if
    return p
end function

' A payout is what reached the bank: its payments, less its refunds and fees.
' Each payment, refund and fee may be paid out once.
function _check_payouts(b, payments)
    p = []
    refunds = _by_id(b.refunds)
    fees = _by_id(b.fees)
    used = {}
    for each po in b.payouts
        id = _get(po, "id", "")
        ccy = _get(po, "currency", "")
        amt = _get(po, "amount", unknown)
        if not _is_date(_get(po, "date", unknown)) then
            p = _problem(p, "payouts", id, "date", "it needs a date")
        end if
        if not _is_money(amt) or money.currency(amt) != ccy then
            p = _problem(p, "payouts", id, "amount", "it needs an amount as money in its currency")
            continue
        end if
        if not _at_minor(amt) then
            p = _problem(p, "payouts", id, "amount", "the amount is finer than the currency's minor unit")
        end if
        net = _zero(ccy)
        listed = false
        groups = [["payments", payments, 1], ["refunds", refunds, -1], ["fees", fees, -1]]
        for each g in groups
            for each item in _list(po, g[0])
                listed = true
                key = g[0] + ":" + string(item)
                if has(used, key) then
                    p = _problem(p, "payouts", id, g[0], "'" + string(item) + "' is already in payout '" + used[key] + "'; paying it out twice counts it twice")
                else
                    used[key] = id
                end if
                table = g[1]
                if not has(table, item) then
                    p = _problem(p, "payouts", id, g[0], "'" + string(item) + "' is not among the bundle's " + g[0])
                else if _get(table[item], "currency", "") != ccy then
                    p = _problem(p, "payouts", id, g[0], "'" + string(item) + "' is not in the payout's currency")
                else if _is_money(_get(table[item], "amount", unknown)) then
                    if g[2] = 1 then
                        net = net + table[item].amount
                    else
                        net = net - table[item].amount
                    end if
                end if
            next
        next
        if listed and net != amt then
            p = _problem(p, "payouts", id, "amount", "its payments less refunds and fees come to " + _amount(net) + " and it says " + _amount(amt))
        end if
    next
    return p
end function

' ---------------------------------------------------------------- profiles

' The profiles, each with what it carries and the options it takes. A profile
' is a target's documented import layout, never a guess at a universal one.
function profiles()
    return ["general", "qbo.invoices", "qbo.bank", "xero.invoices", "xero.credit_notes", "xero.bank", "qbd.iif"]
end function

function _carries(id)
    if id = "general" then
        return ["customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"]
    end if
    if id = "qbo.invoices" or id = "xero.invoices" then
        return ["customers", "invoices"]
    end if
    if id = "xero.credit_notes" then
        return ["customers", "credit_notes"]
    end if
    if id = "qbo.bank" or id = "xero.bank" then
        return ["customers", "payments", "refunds", "fees", "payouts"]
    end if
    if id = "qbd.iif" then
        return ["customers", "invoices", "credit_notes", "payments", "fees", "payouts"]
    end if
    return []
end function

function _options_for(id)
    common = ["batch_id", "skip", "allow_lossy"]
    if id = "general" then
        return _concat(common, ["line_end", "bom"])
    end if
    if id = "qbo.invoices" then
        return _concat(common, ["region", "home_currency", "date_format", "mappings"])
    end if
    if id = "qbo.bank" or id = "xero.bank" then
        return _concat(common, ["account", "currency", "processor", "date_format"])
    end if
    if id = "xero.invoices" or id = "xero.credit_notes" then
        return _concat(common, ["mappings"])
    end if
    if id = "qbd.iif" then
        return _concat(common, ["home_currency", "mappings", "customers"])
    end if
    return common
end function

function _check_profile(id)
    if not contains(profiles(), id) then
        _refuse("render", "'" + string(id) + "' is not a profile; the profiles are " + join(profiles(), ", "))
    end if
    return id
end function

function _opts(id, options)
    allowed = _options_for(id)
    o = {}
    if not is_unknown(options) and not is_nothing(options) then
        if not is_record(options) then
            _refuse("render", "options must be a record")
        end if
        for each k in keys(options)
            if not contains(allowed, k) then
                _refuse("render", "the profile '" + id + "' does not take the option '" + k + "'; it takes " + join(allowed, ", "))
            end if
            o[k] = options[k]
        next
    end if
    return o
end function

' ---------------------------------------------------------------- the plan

' acctexport.plan(b, profile, options) -> what an export WOULD do, nothing
' written: { profile, classification, carried, unsupported, impossible, losses,
' missing_mappings }. `classification` is finio §16's: representable, lossy or
' impossible. A missing mapping is impossible until the caller supplies it.
function plan(b, profile_id, options = unknown)
    id = _check_profile(profile_id)
    o = _opts(id, options)
    v = validate(b)
    st = { impossible: [], losses: [], missing: [] }
    carried = _carries(id)
    skip = _get(o, "skip", [])
    unsupported = []
    for each kind in ["customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"]
        if count(b[kind]) > 0 and not contains(carried, kind) then
            append(unsupported, { type: kind, ids: _ids(b[kind]), skipped: contains(skip, kind) })
        end if
    next
    if id = "qbo.invoices" then
        st = _plan_qbo_invoices(b, o, st)
    else if id = "xero.invoices" then
        st = _plan_xero(b, o, st, "invoices")
    else if id = "xero.credit_notes" then
        st = _plan_xero(b, o, st, "credit_notes")
    else if id = "qbo.bank" or id = "xero.bank" then
        st = _plan_statement(b, o, st, id)
    else if id = "qbd.iif" then
        st = _plan_iif(b, o, st)
        ' the IIF kit has no refund example, so refunds stay unsupported
        if count(b.refunds) > 0 then
            append(unsupported, { type: "refunds", ids: _ids(b.refunds), skipped: contains(skip, "refunds") })
        end if
        loose = []
        for each fe in b.fees
            if not _in_any_payout(b, "fees", fe.id) then
                append(loose, fe.id)
            end if
        next
        if count(loose) > 0 then
            append(unsupported, { type: "fees", ids: loose, skipped: contains(skip, "fees"),
                                  why: "a fee outside a payout has no IIF form; inside one it is a line of the deposit" })
        end if
    end if
    classification = "representable"
    if count(st.losses) > 0 then
        classification = "lossy"
    end if
    if count(st.impossible) > 0 or count(st.missing) > 0 then
        classification = "impossible"
    end if
    return { profile: id, version: _version(), classification: classification,
             valid: v.ok, problems: v.problems, carried: carried, unsupported: unsupported,
             impossible: st.impossible, losses: st.losses, missing_mappings: st.missing }
end function

function _in_any_payout(b, field, id)
    for each po in b.payouts
        if contains(_list(po, field), id) then
            return true
        end if
    next
    return false
end function

function _add(st, which, entry)
    out = st
    list = out[which]
    append(list, entry)
    out[which] = list
    return out
end function

function _need_option(st, o, name, why)
    if not has(o, name) then
        return _add(st, "missing", { mapping: "option " + name, why: why })
    end if
    return st
end function

function _mapped(o, group, key)
    m = _get(o, "mappings", {})
    if not is_record(m) then
        return unknown
    end if
    g = _get(m, group, {})
    if not is_record(g) then
        return unknown
    end if
    if has(g, key) then
        return g[key]
    end if
    return unknown
end function

function _need_map(st, o, group, key, record_type, id)
    if is_unknown(_mapped(o, group, key)) then
        return _add(st, "missing", { mapping: "mappings." + group + "[\"" + key + "\"]", type: record_type, id: id })
    end if
    return st
end function

function _plan_qbo_invoices(b, o, st)
    region = _get(o, "region", "")
    if not contains(["us", "ca", "uk", "au", "other"], region) then
        st = _add(st, "missing", { mapping: "option region", why: "QuickBooks Online's invoice import differs by region (US sales tax cannot be imported; elsewhere every line needs a tax code): us, ca, uk, au or other" })
    end if
    st = _need_option(st, o, "home_currency", "QuickBooks Online's invoice import carries no currency column, so every invoice must be in the company's home currency")
    home = _get(o, "home_currency", "")
    for each inv in b.invoices
        if home != "" and inv.currency != home then
            st = _add(st, "impossible", { type: "invoices", id: inv.id, why: "it is in " + inv.currency + " and the import carries the home currency " + home + " only; nothing here converts currency" })
        end if
        rows = count(inv.lines)
        if rows > 1000 then
            st = _add(st, "impossible", { type: "invoices", id: inv.id, why: "it has " + string(rows) + " lines and one import file holds 1,000 rows" })
        end if
        for each l, i in inv.lines
            tax = _get(l, "tax", _zero(inv.currency))
            if region = "us" and tax > _zero(inv.currency) then
                st = _add(st, "impossible", { type: "invoices", id: inv.id, why: "line " + string(i + 1) + " carries tax, and QuickBooks Online (US) cannot import invoices with sales tax" })
            end if
            if region != "us" and region != "" then
                st = _need_map(st, o, "tax_codes", _get(l, "tax_code", "none"), "invoices", inv.id)
                if tax > _zero(inv.currency) then
                    st = _add(st, "losses", { kind: "tax_recomputed", type: "invoices", id: inv.id,
                                              why: "line " + string(i + 1) + " carries " + _amount(tax) + " of tax; the import takes a tax code and QuickBooks computes the tax, which may differ" })
                end if
            end if
            if _get(l, "discount", _zero(inv.currency)) > _zero(inv.currency) then
                st = _add(st, "losses", { kind: "discount_folded", type: "invoices", id: inv.id,
                                          why: "line " + string(i + 1) + " has a discount, and the import takes no negative charges; the line is written at its discounted amount with no rate, and the discount itself is not carried" })
            end if
            if _is_date(_get(l, "service_to", unknown)) and _get(l, "service_to", unknown) != _get(l, "service_from", unknown) then
                st = _add(st, "losses", { kind: "service_period", type: "invoices", id: inv.id,
                                          why: "line " + string(i + 1) + " has a service period; the import carries one Service Date, so only its first day is written" })
            end if
        next
    next
    return st
end function

function _plan_xero(b, o, st, kind)
    for each d in b[kind]
        for each l, i in d.lines
            st = _need_map(st, o, "accounts", _get(l, "category", "none"), kind, d.id)
            st = _need_map(st, o, "tax_types", _get(l, "tax_code", "none"), kind, d.id)
            disc = _get(l, "discount", _zero(d.currency))
            if disc > _zero(d.currency) then
                pct = _xero_percent(l)
                if is_unknown(pct) then
                    st = _add(st, "losses", { kind: "discount_percent", type: kind, id: d.id,
                                              why: "line " + string(i + 1) + "'s discount is not a whole percentage to two places of its price, and Xero takes Discount as one; Xero would compute a different line amount" })
                end if
            end if
            if _is_date(_get(l, "service_from", unknown)) then
                st = _add(st, "losses", { kind: "service_period", type: kind, id: d.id,
                                          why: "line " + string(i + 1) + " has a service period, and the template has no column for one" })
            end if
        next
    next
    return st
end function

' The Discount column Xero takes: a percentage to two places. Answered only
' when it is EXACT, i.e. when Xero's quantity x unit x (1 - discount%) is the
' line's own amount before rounding; otherwise unknown.
function _xero_percent(l)
    gross = l.unit_price * l.quantity
    disc = _get(l, "discount", _zero(money.currency(gross)))
    if gross <= _zero(money.currency(gross)) then
        return unknown
    end if
    pct = round(number(_full(disc)) * 100 / number(_full(gross)), 2)
    text = string(pct)
    back = gross * number(text) / 100
    if back = disc then
        return text
    end if
    return unknown
end function

function _plan_statement(b, o, st, id)
    acct = _get(o, "account", "")
    if acct != "bank" and acct != "clearing" then
        st = _add(st, "missing", { mapping: "option account", why: "a statement is one account's: \"bank\" (the payouts that reached it) or \"clearing\" (a processor balance: payments in; refunds, fees and payouts out)" })
    end if
    st = _need_option(st, o, "currency", "a statement is in one account's currency")
    ccy = _get(o, "currency", "")
    procs = {}
    for each kind in ["payments", "refunds", "fees", "payouts"]
        for each r in b[kind]
            if acct = "bank" and kind != "payouts" then
                continue
            end if
            if ccy != "" and r.currency != ccy then
                st = _add(st, "impossible", { type: kind, id: r.id, why: "it is in " + r.currency + " and the statement is the " + ccy + " account's; nothing here converts currency" })
            end if
            procs[string(_get(r, "processor", ""))] = true
        next
    next
    if acct = "clearing" and count(keys(procs)) > 1 and not has(o, "processor") then
        st = _add(st, "missing", { mapping: "option processor", why: "the records name " + string(count(keys(procs))) + " processors (" + join(sort(keys(procs)), ", ") + "), and a clearing account is one processor's" })
    end if
    return st
end function

function _plan_iif(b, o, st)
    st = _need_option(st, o, "home_currency", "IIF carries amounts with no currency, so every record must be in the company file's home currency")
    home = _get(o, "home_currency", "")
    for each kind in ["customers", "invoices", "credit_notes", "payments", "fees", "payouts"]
        for each r in b[kind]
            if kind != "customers" and home != "" and _get(r, "currency", home) != home then
                st = _add(st, "impossible", { type: kind, id: r.id, why: "it is in " + r.currency + " and an IIF file carries the home currency " + home + " only" })
            end if
            for each t in _iif_texts(r, kind)
                bad = _iif_bad_text(t)
                if bad != "" then
                    st = _add(st, "impossible", { type: kind, id: r.id, why: bad })
                end if
            next
        next
    next
    for each kind in ["invoices", "credit_notes"]
        for each d in b[kind]
            st = _need_map(st, o, "accounts", "receivable", kind, d.id)
            for each l, i in d.lines
                st = _need_map(st, o, "income_accounts", _get(l, "category", "none"), kind, d.id)
                if _get(l, "tax", _zero(d.currency)) > _zero(d.currency) then
                    st = _add(st, "impossible", { type: kind, id: d.id, why: "line " + string(i + 1) + " carries tax; a taxed IIF transaction needs sales-tax items and vendors, and Intuit's kit advises entering those in QuickBooks" })
                end if
                if _get(l, "discount", _zero(d.currency)) > _zero(d.currency) then
                    st = _add(st, "losses", { kind: "discount_folded", type: kind, id: d.id,
                                              why: "line " + string(i + 1) + " has a discount; the split is written at the discounted amount with no price, and the discount itself is not carried" })
                end if
            next
        next
    next
    for each pay in b.payments
        st = _need_map(st, o, "accounts", "receivable", "payments", pay.id)
        st = _need_map(st, o, "accounts", "undeposited_funds", "payments", pay.id)
        if count(_list(pay, "allocations")) > 0 then
            st = _add(st, "losses", { kind: "payment_link", type: "payments", id: pay.id,
                                      why: "IIF cannot link a payment to the invoices it pays (Intuit's IIF manual); the payment imports against Accounts Receivable unapplied" })
        end if
    next
    for each po in b.payouts
        st = _need_map(st, o, "accounts", "bank", "payouts", po.id)
        if count(_list(po, "payments")) > 0 then
            st = _need_map(st, o, "accounts", "undeposited_funds", "payouts", po.id)
        end if
        if count(_list(po, "fees")) > 0 then
            st = _need_map(st, o, "accounts", "processor_fees", "payouts", po.id)
        end if
        if count(_list(po, "refunds")) > 0 then
            st = _add(st, "impossible", { type: "payouts", id: po.id, why: "it nets refunds, and refunds have no IIF form here (Intuit's kit has no refund example)" })
        end if
        if count(_list(po, "payments")) + count(_list(po, "fees")) = 0 then
            st = _add(st, "impossible", { type: "payouts", id: po.id, why: "a deposit is written from the payments it carries, and this payout lists none" })
        end if
    next
    return st
end function

function _iif_texts(r, kind)
    out = []
    for each f in ["id", "name", "number", "memo", "reference", "description", "email", "processor"]
        v = _get(r, f, unknown)
        if is_string(v) then
            append(out, v)
        end if
    next
    if kind = "invoices" or kind = "credit_notes" then
        for each l in r.lines
            append(out, string(_get(l, "description", "")))
        next
    end if
    return out
end function

' IIF is ASCII and tab-separated (Intuit's kit), so a field holding anything
' else cannot be written as it is. Refused, never transliterated.
function _iif_bad_text(t)
    n = byte_count(t)
    for i = 0 to n - 1
        c = byte_at(t, i)
        if c < 32 or c > 126 then
            return "'" + t + "' holds a byte IIF's ASCII text cannot carry (" + string(c) + "): a tab, a line break or a character outside ASCII"
        end if
        if c = 34 then
            return "'" + t + "' holds a double quote, which a spreadsheet reading an IIF file takes as quoting"
        end if
    next
    return ""
end function

' ---------------------------------------------------------------- rendering

function _csv(header, rows, line_end, bom)
    out = []
    if bom then
        append(out, from_bytes([239, 187, 191]))
    end if
    append(out, csv.line(header, { line_end: line_end }))
    for each r in rows
        append(out, csv.line(r, { line_end: line_end }))
    next
    return join(out, "")
end function

function _file(name, text, rows, kind)
    return { name: name, text: text, rows: rows, format: kind }
end function

function _customer_name(b, id)
    for each c in b.customers
        if c.id = id then
            return c.name
        end if
    next
    return ""
end function

function _payout_of(b, field, id)
    for each po in b.payouts
        if contains(_list(po, field), id) then
            return po.id
        end if
    next
    return ""
end function

function _sum(list, ccy)
    s = _zero(ccy)
    for each m in list
        s = s + m
    next
    return s
end function

function _record(kind, id, number, file, key)
    return { type: kind, id: id, number: number, file: file, key: key }
end function

' --- general: every record type, one CSV each, nothing lost ----------------
function _render_general(b, o)
    le = _get(o, "line_end", chr(13) + "\n")
    bom = _get(o, "bom", false)
    files = []
    recs = []
    rows = []
    for each c in b.customers
        a = _get(c, "address", {})
        append(rows, [c.id, c.name, _get(c, "email", ""), _get(a, "line1", ""), _get(a, "line2", ""), _get(a, "city", ""),
                      _get(a, "region", ""), _get(a, "postal_code", ""), _get(a, "country", "")])
        append(recs, _record("customers", c.id, "", "customers.csv", c.id))
    next
    append(files, _file("customers.csv", _csv(["customer_id", "name", "email", "address_line1", "address_line2", "city", "region", "postal_code", "country"], rows, le, bom), count(rows), "csv"))
    for each kind in ["invoices", "credit_notes"]
        stem = kind
        key = "invoice_id"
        if kind = "credit_notes" then
            key = "credit_note_id"
        end if
        heads = []
        lines = []
        for each d in b[kind]
            sub = _zero(d.currency)
            tax = _zero(d.currency)
            for each l, i in d.lines
                sub = sub + l.amount
                t = _get(l, "tax", _zero(d.currency))
                tax = tax + t
                disc = _get(l, "discount", unknown)
                append(lines, [d.id, _get(l, "id", ""), string(i + 1), l.description, _num(l.quantity), _full(l.unit_price),
                               _pick(is_unknown(disc), "", disc), _amount(l.amount), _amount(t), _get(l, "tax_code", ""),
                               _get(l, "category", ""), _get(l, "item", ""), _iso(_get(l, "service_from", unknown)), _iso(_get(l, "service_to", unknown))])
            next
            row = [d.id, d.number, d.customer, _iso(d.issue_date), _iso(_get(d, "due_date", unknown)), d.currency,
                   _amount(sub), _amount(tax), _amount(d.total), _get(d, "memo", "")]
            if kind = "credit_notes" then
                append(row, _get(d, "applies_to", ""))
            end if
            append(heads, row)
            append(recs, _record(kind, d.id, d.number, stem + ".csv", d.id))
        next
        h = [key, "number", "customer_id", "issue_date", "due_date", "currency", "subtotal", "tax_total", "total", "memo"]
        if kind = "credit_notes" then
            append(h, "applies_to_invoice_id")
        end if
        append(files, _file(stem + ".csv", _csv(h, heads, le, bom), count(heads), "csv"))
        append(files, _file(left(stem, len(stem) - 1) + "_lines.csv",
                            _csv([key, "line_id", "line_no", "description", "quantity", "unit_price", "discount", "amount", "tax",
                                  "tax_code", "category", "item", "service_from", "service_to"], lines, le, bom), count(lines), "csv"))
    next
    pays = []
    allocs = []
    for each p in b.payments
        allocated = _zero(p.currency)
        for each a in _list(p, "allocations")
            allocated = allocated + a.amount
            append(allocs, [p.id, a.invoice, p.currency, _amount(a.amount)])
        next
        append(pays, [p.id, p.customer, _iso(p.date), p.currency, _amount(p.amount), _amount(allocated), _amount(p.amount - allocated),
                      _get(p, "method", ""), _get(p, "reference", ""), _get(p, "processor", ""), _payout_of(b, "payments", p.id)])
        append(recs, _record("payments", p.id, "", "payments.csv", p.id))
    next
    append(files, _file("payments.csv", _csv(["payment_id", "customer_id", "date", "currency", "gross_amount", "allocated", "unallocated",
                                              "method", "reference", "processor", "payout_id"], pays, le, bom), count(pays), "csv"))
    append(files, _file("payment_allocations.csv", _csv(["payment_id", "invoice_id", "currency", "amount"], allocs, le, bom), count(allocs), "csv"))
    rows = []
    for each r in b.refunds
        append(rows, [r.id, r.customer, _iso(r.date), r.currency, _amount(r.amount), _get(r, "payment", ""), _get(r, "credit_note", ""),
                      _get(r, "reference", ""), _get(r, "processor", ""), _payout_of(b, "refunds", r.id)])
        append(recs, _record("refunds", r.id, "", "refunds.csv", r.id))
    next
    append(files, _file("refunds.csv", _csv(["refund_id", "customer_id", "date", "currency", "amount", "payment_id", "credit_note_id",
                                             "reference", "processor", "payout_id"], rows, le, bom), count(rows), "csv"))
    rows = []
    for each f in b.fees
        append(rows, [f.id, _iso(f.date), f.currency, _amount(f.amount), _get(f, "processor", ""), _get(f, "payment", ""),
                      _payout_of(b, "fees", f.id), _get(f, "description", "")])
        append(recs, _record("fees", f.id, "", "fees.csv", f.id))
    next
    append(files, _file("fees.csv", _csv(["fee_id", "date", "currency", "amount", "processor", "payment_id", "payout_id", "description"], rows, le, bom), count(rows), "csv"))
    rows = []
    items = []
    pays_by = _by_id(b.payments)
    refs_by = _by_id(b.refunds)
    fees_by = _by_id(b.fees)
    for each po in b.payouts
        g = []
        for each id in _list(po, "payments")
            append(g, pays_by[id].amount)
            append(items, [po.id, "payment", id, po.currency, _amount(pays_by[id].amount)])
        next
        rf = []
        for each id in _list(po, "refunds")
            append(rf, refs_by[id].amount)
            append(items, [po.id, "refund", id, po.currency, _amount(-refs_by[id].amount)])
        next
        fe = []
        for each id in _list(po, "fees")
            append(fe, fees_by[id].amount)
            append(items, [po.id, "fee", id, po.currency, _amount(-fees_by[id].amount)])
        next
        append(rows, [po.id, _iso(po.date), po.currency, _amount(_sum(g, po.currency)), _amount(_sum(rf, po.currency)),
                      _amount(_sum(fe, po.currency)), _amount(po.amount), _get(po, "processor", ""), _get(po, "reference", "")])
        append(recs, _record("payouts", po.id, "", "payouts.csv", po.id))
    next
    append(files, _file("payouts.csv", _csv(["payout_id", "date", "currency", "gross_payments", "refunds", "fees", "net_amount",
                                             "processor", "reference"], rows, le, bom), count(rows), "csv"))
    append(files, _file("payout_items.csv", _csv(["payout_id", "item_type", "item_id", "currency", "effect_on_payout"], items, le, bom), count(items), "csv"))
    rows = []
    for each kind in ["customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"]
        for each r in b[kind]
            ext = _get(r, "external", {})
            if is_record(ext) then
                for each k in sort(keys(ext))
                    append(rows, [kind, r.id, k, string(ext[k])])
                next
            end if
        next
    next
    append(files, _file("external_references.csv", _csv(["record_type", "record_id", "key", "value"], rows, le, bom), count(rows), "csv"))
    append(files, _file("README.txt", _general_readme(le), 0, "text"))
    return { files: files, records: recs, date_format: "YYYY-MM-DD" }
end function

function _general_readme(le)
    t = ["Accounting export (gBASIC acctexport, profile general/" + _version() + ")",
         "",
         "Revenue is the invoices (invoices.csv, invoice_lines.csv), less credit notes.",
         "Payments settle invoices; they are not revenue. Their allocations to invoices",
         "are in payment_allocations.csv, and any unallocated part is shown.",
         "Refunds return money; fees are what a payment processor withheld (an expense);",
         "a payout is a transfer of what was left to the bank, not new revenue.",
         "payout_items.csv lists what each payout contains: its net is its payments",
         "less its refunds and fees.",
         "",
         "Amounts are exact decimals in the currency named on each row, at the",
         "currency's minor unit; unit prices keep every digit they hold. Dates are",
         "YYYY-MM-DD. Nothing has been converted between currencies, and no account",
         "codes, tax treatments or journal entries have been assigned.",
         "",
         "manifest.json lists every record, file, count, total and checksum in this batch."]
    return join(t, le) + le
end function

' --- QuickBooks Online: invoices -------------------------------------------
function _render_qbo_invoices(b, o)
    region = o.region
    fmt = _get(o, "date_format", _pick(region = "us", "MM/DD/YYYY", "DD/MM/YYYY"))
    head = ["InvoiceNo", "Customer", "InvoiceDate", "DueDate", "Terms", "Memo", "Item(Product/Service)", "ItemDescription",
            "ItemQuantity", "ItemRate", "ItemAmount", "Service Date"]
    if region != "us" then
        append(head, "ItemTaxCode")
    end if
    files = []
    recs = []
    chunk = []
    chunk_invoices = 0
    n = 1
    for each inv in b.invoices
        rows = []
        for each l in inv.lines
            disc = _get(l, "discount", _zero(inv.currency))
            rate = _full(l.unit_price)
            if disc > _zero(inv.currency) then
                rate = ""
            end if
            item = _get(l, "item", "")
            mapped = _mapped(o, "items", item)
            if not is_unknown(mapped) then
                item = mapped
            end if
            r = [inv.number, _qbo_customer(b, o, inv.customer), _date_as(inv.issue_date, fmt), _date_as(inv.due_date, fmt), "",
                 _get(inv, "memo", ""), item, l.description, _num(l.quantity), rate, _amount(l.amount),
                 _date_as(_get(l, "service_from", unknown), fmt)]
            if region != "us" then
                append(r, _mapped(o, "tax_codes", _get(l, "tax_code", "none")))
            end if
            append(rows, r)
        next
        ' a new file before the 101st invoice or the 1,001st row
        if chunk_invoices = 100 or count(chunk) + count(rows) > 1000 then
            append(files, _file(_seq("qbo_invoices", n), _csv(head, chunk, chr(13) + "\n", false), count(chunk), "csv"))
            n = n + 1
            chunk = []
            chunk_invoices = 0
        end if
        chunk = _concat(chunk, rows)
        chunk_invoices = chunk_invoices + 1
        append(recs, _record("invoices", inv.id, inv.number, _seq("qbo_invoices", n), inv.number))
    next
    if count(chunk) > 0 then
        append(files, _file(_seq("qbo_invoices", n), _csv(head, chunk, chr(13) + "\n", false), count(chunk), "csv"))
    end if
    return { files: files, records: recs, date_format: fmt }
end function

function _qbo_customer(b, o, id)
    m = _mapped(o, "customers", id)
    if not is_unknown(m) then
        return m
    end if
    return _customer_name(b, id)
end function

function _seq(stem, n)
    s = string(n)
    while len(s) < 3
        s = "0" + s
    end while
    return stem + "_" + s + ".csv"
end function

' --- a statement: a bank account's or a processor clearing account's -------
function _movements(b, o)
    acct = o.account
    ccy = o.currency
    proc = _get(o, "processor", "")
    order = { payments: 1, refunds: 2, fees: 3, payouts: 4 }
    out = []
    for each kind in ["payments", "refunds", "fees", "payouts"]
        if acct = "bank" and kind != "payouts" then
            continue
        end if
        for each r in b[kind]
            if r.currency != ccy then
                continue
            end if
            if proc != "" and _get(r, "processor", "") != proc then
                continue
            end if
            amt = r.amount
            desc = ""
            payee = ""
            if kind = "payments" then
                payee = _customer_name(b, r.customer)
                desc = "Payment received " + _get(r, "reference", r.id)
            else if kind = "refunds" then
                amt = -r.amount
                payee = _customer_name(b, r.customer)
                desc = "Refund " + _get(r, "reference", r.id)
            else if kind = "fees" then
                amt = -r.amount
                payee = _get(r, "processor", "")
                desc = "Processor fee " + _get(r, "description", r.id)
            else
                payee = _get(r, "processor", "")
                if acct = "clearing" then
                    amt = -r.amount
                    desc = "Payout to bank " + _get(r, "reference", r.id)
                else
                    desc = "Payout from " + _get(r, "processor", "processor") + " " + _get(r, "reference", r.id)
                end if
            end if
            append(out, { date: r.date, order: order[kind], id: r.id, kind: kind, amount: amt, payee: payee, description: desc })
        next
    next
    return sort(out, { by: ["date", "order", "id"] })
end function

function _render_statement(b, o, id)
    fmt = _get(o, "date_format", "DD/MM/YYYY")
    moves = _movements(b, o)
    rows = []
    recs = []
    name = replace(id, ".", "_") + "_" + o.account + ".csv"
    for each m in moves
        if id = "qbo.bank" then
            append(rows, [_date_as(m.date, fmt), m.description, _amount(m.amount)])
        else
            append(rows, [_date_as(m.date, fmt), _amount(m.amount), m.payee, m.description, m.id])
        end if
        append(recs, _record(m.kind, m.id, "", name, m.id))
    next
    head = ["Date", "Amount", "Payee", "Description", "Reference"]
    if id = "qbo.bank" then
        head = ["Date", "Description", "Amount"]
    end if
    return { files: [_file(name, _csv(head, rows, chr(13) + "\n", false), count(rows), "csv")], records: recs, date_format: fmt }
end function

' --- Xero: sales invoices or credit notes -----------------------------------
function _render_xero(b, o, kind)
    head = ["*ContactName", "EmailAddress", "POAddressLine1", "POAddressLine2", "POAddressLine3", "POAddressLine4", "POCity", "PORegion",
            "POPostalCode", "POCountry", "*InvoiceNumber", "Reference", "*InvoiceDate", "*DueDate", "Total", "InventoryItemCode",
            "*Description", "*Quantity", "*UnitAmount", "Discount", "*AccountCode", "*TaxType", "TaxAmount", "TrackingName1",
            "TrackingOption1", "TrackingName2", "TrackingOption2", "Currency", "BrandingTheme"]
    stem = "xero_sales_invoices"
    sign = ""
    if kind = "credit_notes" then
        stem = "xero_credit_notes"
        sign = "-"
    end if
    customers = _by_id(b.customers)
    files = []
    recs = []
    chunk = []
    n = 1
    for each d in b[kind]
        c = customers[d.customer]
        a = _get(c, "address", {})
        rows = []
        for each l in d.lines
            pct = ""
            if _get(l, "discount", _zero(d.currency)) > _zero(d.currency) then
                pct = default(_xero_percent(l), "")
            end if
            item = _get(l, "item", "")
            if not is_unknown(_mapped(o, "items", item)) then
                item = _mapped(o, "items", item)
            end if
            append(rows, [c.name, _get(c, "email", ""), _get(a, "line1", ""), _get(a, "line2", ""), "", "", _get(a, "city", ""),
                          _get(a, "region", ""), _get(a, "postal_code", ""), _get(a, "country", ""), d.number, d.id,
                          _date_as(d.issue_date, "DD/MM/YYYY"), _date_as(_get(d, "due_date", d.issue_date), "DD/MM/YYYY"), "", item,
                          l.description, _num(l.quantity), sign + _full(l.unit_price), pct,
                          _mapped(o, "accounts", _get(l, "category", "none")), _mapped(o, "tax_types", _get(l, "tax_code", "none")),
                          _amount(_get(l, "tax", _zero(d.currency))), "", "", "", "", d.currency, ""])
        next
        ' Xero recommends no more than 500 items a file; an invoice is never split
        if count(chunk) > 0 and count(chunk) + count(rows) > 500 then
            append(files, _file(_seq(stem, n), _csv(head, chunk, chr(13) + "\n", false), count(chunk), "csv"))
            n = n + 1
            chunk = []
        end if
        chunk = _concat(chunk, rows)
        append(recs, _record(kind, d.id, d.number, _seq(stem, n), d.number))
    next
    if count(chunk) > 0 then
        append(files, _file(_seq(stem, n), _csv(head, chunk, chr(13) + "\n", false), count(chunk), "csv"))
    end if
    return { files: files, records: recs, date_format: "DD/MM/YYYY" }
end function

' --- QuickBooks Desktop: IIF ----------------------------------------------
' Tab-separated ASCII with CRLF line ends, a header block per transaction type,
' and signs as in Intuit's IIF Import Kit examples (QuickBooks 2007 and later).
function _iif_line(fields)
    return join(fields, chr(9)) + chr(13) + "\n"
end function

function _render_iif(b, o)
    acc = _get(_get(o, "mappings", {}), "accounts", {})
    out = []
    recs = []
    name = "qbd.iif"
    rows = 0
    if _get(o, "customers", true) and count(b.customers) > 0 then
        append(out, _iif_line(["!CUST", "NAME", "BADDR1", "BADDR2", "BADDR3", "BADDR4", "EMAIL"]))
        for each c in b.customers
            a = _get(c, "address", {})
            append(out, _iif_line(["CUST", c.name, _get(a, "line1", ""), _get(a, "line2", ""),
                                   trim(_get(a, "city", "") + " " + _get(a, "region", "") + " " + _get(a, "postal_code", "")),
                                   _get(a, "country", ""), _get(c, "email", "")]))
            append(recs, _record("customers", c.id, "", name, c.name))
            rows = rows + 1
        next
    end if
    trns_head = _iif_line(["!TRNS", "TRNSID", "TRNSTYPE", "DATE", "ACCNT", "NAME", "AMOUNT", "DOCNUM", "MEMO", "DUEDATE"])
    spl_head = _iif_line(["!SPL", "SPLID", "TRNSTYPE", "DATE", "ACCNT", "NAME", "AMOUNT", "DOCNUM", "MEMO", "QNTY", "PRICE", "INVITEM"])
    end_head = _iif_line(["!ENDTRNS"])
    wrote_head = false
    for each kind in ["invoices", "credit_notes"]
        type_name = "INVOICE"
        s = 1
        if kind = "credit_notes" then
            type_name = "CREDIT MEMO"
            s = -1
        end if
        for each d in b[kind]
            if not wrote_head then
                append(out, trns_head)
                append(out, spl_head)
                append(out, end_head)
                wrote_head = true
            end if
            date = _date_as(d.issue_date, "M/D/YYYY")
            cust = _customer_name(b, d.customer)
            total = d.total
            if s < 0 then
                total = -total
            end if
            append(out, _iif_line(["TRNS", "", type_name, date, acc.receivable, cust, _amount(total), d.number,
                                   _get(d, "memo", ""), _date_as(_get(d, "due_date", unknown), "M/D/YYYY")]))
            for each l in d.lines
                amt = -l.amount
                qty = -l.quantity
                if s < 0 then
                    amt = l.amount
                    qty = l.quantity
                end if
                price = _full(l.unit_price)
                if _get(l, "discount", _zero(d.currency)) > _zero(d.currency) then
                    price = ""
                end if
                item = default(_mapped(o, "items", _get(l, "item", "")), _get(l, "item", ""))
                append(out, _iif_line(["SPL", "", type_name, date, _mapped(o, "income_accounts", _get(l, "category", "none")), "",
                                       _amount(amt), d.number, l.description, _num(qty), price, item]))
            next
            append(out, _iif_line(["ENDTRNS"]))
            append(recs, _record(kind, d.id, d.number, name, d.number))
            rows = rows + 1
        next
    next
    for each p in b.payments
        if not wrote_head then
            append(out, trns_head)
            append(out, spl_head)
            append(out, end_head)
            wrote_head = true
        end if
        date = _date_as(p.date, "M/D/YYYY")
        cust = _customer_name(b, p.customer)
        ref = _get(p, "reference", p.id)
        append(out, _iif_line(["TRNS", "", "PAYMENT", date, acc.undeposited_funds, cust, _amount(p.amount), ref, "Payment " + p.id, ""]))
        append(out, _iif_line(["SPL", "", "PAYMENT", date, acc.receivable, cust, _amount(-p.amount), ref, "", "", "", ""]))
        append(out, _iif_line(["ENDTRNS"]))
        append(recs, _record("payments", p.id, "", name, ref))
        rows = rows + 1
    next
    pays = _by_id(b.payments)
    fees = _by_id(b.fees)
    for each po in b.payouts
        if not wrote_head then
            append(out, trns_head)
            append(out, spl_head)
            append(out, end_head)
            wrote_head = true
        end if
        date = _date_as(po.date, "M/D/YYYY")
        ref = _get(po, "reference", po.id)
        append(out, _iif_line(["TRNS", "", "DEPOSIT", date, acc.bank, "", _amount(po.amount), ref, "Payout " + po.id, ""]))
        for each id in _list(po, "payments")
            pp = pays[id]
            append(out, _iif_line(["SPL", "", "DEPOSIT", date, acc.undeposited_funds, _customer_name(b, pp.customer),
                                   _amount(-pp.amount), ref, "Payment " + id, "", "", ""]))
        next
        for each id in _list(po, "fees")
            append(out, _iif_line(["SPL", "", "DEPOSIT", date, acc.processor_fees, "", _amount(fees[id].amount), ref,
                                   "Processor fee " + id, "", "", ""]))
            append(recs, _record("fees", id, "", name, ref))
        next
        append(out, _iif_line(["ENDTRNS"]))
        append(recs, _record("payouts", po.id, "", name, ref))
        rows = rows + 1
    next
    return { files: [_file(name, join(out, ""), rows, "iif")], records: recs, date_format: "M/D/YYYY" }
end function

' ---------------------------------------------------------------- export

' acctexport.render(b, profile, options) -> { batch_id, profile, files:
' [{ name, text, rows, format }], manifest, manifest_text, plan }.
' Refused, naming every reason, when the bundle does not validate, when a
' record type is not carried and not in `skip`, when the plan is impossible
' (a mapping missing included), or when it is lossy in a way `allow_lossy`
' does not name.
function render(b, profile_id, options = unknown)
    id = _check_profile(profile_id)
    o = _opts(id, options)
    pl = plan(b, id, o)
    if not pl.valid then
        whys = []
        for each pr in pl.problems
            append(whys, pr.type + " '" + pr.id + "' " + pr.field + ": " + pr.why)
        next
        _refuse("render", string(count(whys)) + " problem(s) in the bundle: " + join(whys, "; "))
    end if
    for each u in pl.unsupported
        if not u.skipped then
            _refuse("render", ("the profile '" + id + "' does not carry " + u.type + " (" + join(u.ids, ", ") + ")"
                               + default(_get(u, "why", unknown), "") + "; list \"" + u.type + "\" in `skip` to leave them out, and the manifest will say so"))
        end if
    next
    if count(pl.missing_mappings) > 0 then
        whys = []
        for each m in pl.missing_mappings
            append(whys, m.mapping + default(_get(m, "why", unknown), (" (for " + string(_get(m, "type", "")) + " '" + string(_get(m, "id", "")) + "')")))
        next
        _refuse("render", "the profile '" + id + "' needs what the bundle cannot supply: " + join(unique(whys), "; "))
    end if
    if count(pl.impossible) > 0 then
        whys = []
        for each m in pl.impossible
            append(whys, m.type + " '" + m.id + "': " + m.why)
        next
        _refuse("render", "cannot be written as '" + id + "': " + join(whys, "; "))
    end if
    allowed = _get(o, "allow_lossy", [])
    for each l in pl.losses
        if not contains(allowed, l.kind) then
            _refuse("render", ("writing '" + id + "' would lose something: " + l.type + " '" + l.id + "': " + l.why
                               + ". Name \"" + l.kind + "\" in `allow_lossy` to accept that loss; it is recorded in the manifest"))
        end if
    next
    skip = _get(o, "skip", [])
    view = b
    for each kind in skip
        if has(view, kind) then
            view[kind] = []
        end if
    next
    if id = "qbd.iif" and contains(skip, "fees") then
        ' a fee inside a payout is a line of its deposit: kept with its payout
        kept = []
        for each fe in b.fees
            if _in_any_payout(b, "fees", fe.id) then
                append(kept, fe)
            end if
        next
        view.fees = kept
    end if
    r = unknown
    if id = "general" then
        r = _render_general(view, o)
    else if id = "qbo.invoices" then
        r = _render_qbo_invoices(view, o)
    else if id = "qbo.bank" or id = "xero.bank" then
        r = _render_statement(view, o, id)
    else if id = "xero.invoices" then
        r = _render_xero(view, o, "invoices")
    else if id = "xero.credit_notes" then
        r = _render_xero(view, o, "credit_notes")
    else
        r = _render_iif(view, o)
    end if
    return _finish(b, view, id, o, pl, r)
end function

' The manifest, and the batch id it is keyed by.
function _finish(b, view, id, o, pl, r)
    files = []
    digest = [id + "/" + _version()]
    for each f in r.files
        h = hex_encode(sha256(f.text))
        append(files, { name: f.name, format: f.format, rows: f.rows, bytes: byte_count(f.text), sha256: h })
        append(digest, f.name + " " + h)
    next
    batch = _get(o, "batch_id", "")
    if batch = "" then
        batch = "ax-" + left(hex_encode(sha256(join(digest, "\n"))), 16)
    end if
    counts = {}
    totals = []
    for each kind in ["customers", "invoices", "credit_notes", "payments", "refunds", "fees", "payouts"]
        n = 0
        sums = {}
        for each rec in r.records
            if rec.type = kind then
                n = n + 1
            end if
        next
        counts[kind] = n
        if kind != "customers" then
            by = _by_id(view[kind])
            seen = {}
            for each rec in r.records
                if rec.type = kind and has(by, rec.id) and not has(seen, rec.id) then
                    seen[rec.id] = true
                    src = by[rec.id]
                    amt = _get(src, "total", _get(src, "amount", unknown))
                    c = money.currency(amt)
                    if has(sums, c) then
                        sums[c] = sums[c] + amt
                    else
                        sums[c] = amt
                    end if
                end if
            next
            for each c in sort(keys(sums))
                append(totals, { type: kind, currency: c, amount: _amount(sums[c]) })
            next
        end if
    next
    skipped = []
    for each u in pl.unsupported
        append(skipped, { type: u.type, ids: u.ids, why: "the profile does not carry them; the caller listed them in `skip`" })
    next
    losses = []
    for each l in pl.losses
        append(losses, { kind: l.kind, type: l.type, id: l.id, why: l.why })
    next
    records = []
    for each rec in r.records
        append(records, { type: rec.type, id: rec.id, number: rec.number, file: rec.file, target_key: rec.key })
    next
    m = { manifest: "gbasic.acctexport/" + _version(), batch_id: batch, profile: id, profile_version: _version(),
          organization: b.organization.name, date_format: r.date_format, files: files, counts: counts, totals: totals,
          records: records, skipped: skipped, accepted_losses: losses,
          status: "generated",
          note: ("These files were generated, not imported. Whether the receiving product imported them, and whether it "
                 + "created duplicates, is not known here; record the outcome against this batch_id.") }
    return { batch_id: batch, profile: id, files: r.files, manifest: m, manifest_text: json_encode(m) + "\n", plan: pl }
end function

' acctexport.save(result, dir [, { overwrite }]) -> the paths written: each
' file, then manifest.json. An existing file is refused unless `overwrite` is
' true, so a batch already handed over is not replaced by accident.
function save(result, dir, options = unknown)
    over = false
    if is_record(options) then
        for each k in keys(options)
            if k != "overwrite" then
                _refuse("save", "it takes the option overwrite, not '" + k + "'")
            end if
        next
        over = _get(options, "overwrite", false)
    end if
    folder {file}= dir
    if not exists(folder) then
        _refuse("save", "the folder '" + string(dir) + "' does not exist")
    end if
    names = []
    for each f in result.files
        append(names, f.name)
    next
    append(names, "manifest.json")
    if not over then
        for each nm in names
            path {file}= join_path(string(dir), nm)
            if exists(path) then
                _refuse("save", "'" + nm + "' already exists in " + string(dir) + "; give `overwrite: true` to replace it, or write the batch elsewhere")
            end if
        next
    end if
    paths = []
    for each f in result.files
        append(paths, _put(string(dir), f.name, f.text))
    next
    append(paths, _put(string(dir), "manifest.json", result.manifest_text))
    return paths
end function

' A file appears whole or not at all: written beside its destination, then
' moved onto it.
function _put(dir, name, text)
    t = temp_file({ in: dir })
    write(t, text)
    path {file}= join_path(dir, name)
    x = atomic_replace(t, path)
    return string(path)
end function

' ---------------------------------------------------------------- registry

' Each profile's registry entry (finio §9): what the target is, where its
' layout was read and when, what is known to vary, and what is missing.
' Checked by finio.check_registry_entry.
function registry_entry(profile_id)
    id = _check_profile(profile_id)
    day = "2026-11-10"
    base = { id: "acctexport." + id, family: "accounting import", domain: "accounts receivable and cash for an outside accountant",
             transport: "a file the accountant imports", acquisition_class: "PUBLIC_VENDOR", spec_public: true,
             implementation_allowed: true, spec_redistribution_allowed: false, sample_redistribution_allowed: false,
             state: "researched", recognition_status: "not applicable: this library writes and never reads",
             read_status: "not implemented", validation_status: "local layout checks only; no import into the vendor's product was made",
             last_reviewed: day, next_review_due: "2027-05-10", maintenance_priority: "periodic" }
    if id = "general" then
        base.name = "gBASIC general accountant bundle"
        base.authority = "this project"
        base.acquisition_class = "OPEN"
        base.state = "implemented"
        base.description = "One CSV per record type and per link (allocations, payout items), a README and a manifest; nothing is lost."
        base.representation = "CSV (RFC 4180, UTF-8, CRLF)"
        base.specification_sources = [{ source_type: "specification", source_url_or_reference: "docs/accounting_export_design.md", date_retrieved: day }]
        base.write_status = "implemented"
    else if id = "qbo.invoices" then
        base.name = "QuickBooks Online invoice import (CSV)"
        base.authority = "Intuit"
        base.description = "Rows of invoice lines grouped by InvoiceNo; the date format is chosen at upload; columns are mapped at upload."
        base.representation = "CSV"
        base.specification_sources = [
            { source_type: "vendor_documentation", source_url_or_reference: "Intuit PDF 'Make sure your invoices import correctly': https://digitalasset.intuit.com/DOCUMENT/A5EA0szyV/import-invoices-quickbooks-online-guide.pdf", date_retrieved: day, note: "required columns; 1,000 rows; display names; custom transaction numbers" },
            { source_type: "vendor_documentation", source_url_or_reference: "Intuit 'Import multiple invoices at once' (UK): https://quickbooks.intuit.com/learn-support/en-uk/help-article/import-export-files/import-multiple-invoices/L7E9Xrd8l_GB_en_GB", date_retrieved: day, note: "100 invoices per import; no negative charges; item tax code outside the US" },
            { source_type: "vendor_documentation", source_url_or_reference: "Intuit 'Common questions about importing data to QuickBooks Online' (global): https://quickbooks.intuit.com/learn-support/en-global/help-article/import-export-files/common-questions-importing-data-quickbooks-online/L4OYJRFdj_ROW_en", date_retrieved: day, note: "which data types import; payments are not among them" },
            { source_type: "public_sample", source_url_or_reference: "Intuit community, sample-file headers quoted 2020-08-10: https://quickbooks.intuit.com/learn-support/en-us/other-questions/when-an-invoice-has-multiple-rows-enter-the-same-invoice-number/00/257408", date_retrieved: day, note: "column names; the sample file itself is behind a login" } ]
        base.known_variants = ["the US edition refuses invoices with sales tax set up; other regions map tax codes",
                               "the invoice search summary reports 100 invoices per import and 1,000 rows per file"]
        base.write_status = "implemented; payments, refunds and payouts are not file imports (the API is out of scope)"
    else if id = "qbo.bank" then
        base.name = "QuickBooks Online bank transaction upload (CSV, 3-column)"
        base.authority = "Intuit"
        base.description = "Date, Description, Amount: money in positive, money out negative."
        base.representation = "CSV"
        base.specification_sources = [{ source_type: "vendor_documentation", source_url_or_reference: "Intuit 'Prepare your CSV file for bank upload to QuickBooks' (UK): https://quickbooks.intuit.com/learn-support/en-uk/help-article/bank-transactions/format-csv-files-excel-get-bank-transactions/L4BjLWckq_GB_en_GB", date_retrieved: day }]
        base.known_variants = ["Intuit advises keeping numbers out of Description; references are kept in it here, for matching"]
        base.write_status = "implemented"
    else if id = "xero.invoices" or id = "xero.credit_notes" then
        base.name = "Xero sales invoice / credit note import (CSV template)"
        base.authority = "Xero"
        base.description = "One row per line, grouped by InvoiceNumber; DD/MM/YYYY; imported as drafts; credit notes take a negative UnitAmount."
        base.representation = "CSV"
        base.specification_sources = [
            { source_type: "vendor_documentation", source_url_or_reference: "Xero Central 'Import customer invoices': https://central.xero.com/0/article/Import-customer-invoices", date_retrieved: day },
            { source_type: "vendor_documentation", source_url_or_reference: "Xero Central 'Import customer credit notes' (UK): https://central.xero.com/0/article/Import-a-customer-credit-note-UK", date_retrieved: day } ]
        base.known_variants = ["the template's column ORDER is in a file behind a login; the order used is the one third-party guides agree on, and every name is in Xero's articles"]
        base.write_status = "implemented"
    else if id = "xero.bank" then
        base.name = "Xero bank statement import (CSV)"
        base.authority = "Xero"
        base.description = "Date and Amount required; Payee, Description, Reference optional; one Amount column, money out negative."
        base.representation = "CSV"
        base.specification_sources = [{ source_type: "vendor_documentation", source_url_or_reference: "Xero Central 'Import a bank statement in CSV format' (US): https://central.xero.com/0/article/Import-a-CSV-bank-statement-US", date_retrieved: day }]
        base.known_variants = ["the template's headings are regional and mapped at import"]
        base.write_status = "implemented"
    else
        base.name = "QuickBooks Desktop IIF (Intuit Interchange Format)"
        base.authority = "Intuit"
        base.description = "Tab-separated ASCII, CRLF; TRNS/SPL/ENDTRNS transactions under !TRNS/!SPL/!ENDTRNS headers; lists under !CUST."
        base.representation = "IIF (tab-separated text)"
        base.specification_sources = [
            { source_type: "vendor_documentation", source_url_or_reference: "Intuit 'IIF Overview: import kit, sample files, and headers' (updated 8/4/2026): https://quickbooks.intuit.com/learn-support/en-us/help-article/list-management/iif-overview-import-kit-sample-files-headers/L5CZIpJne_US_en_US", date_retrieved: day },
            { source_type: "public_sample", source_url_or_reference: "Intuit IIF Import Kit: https://support.quickbooks.intuit.com/support/pages/executable/iif/iif_import_kit.zip (manual dated 9/30/2008; examples for QuickBooks 2007 and later)", date_retrieved: day, note: "not redistributed: its terms are not stated" } ]
        base.known_variants = ["taxed transactions need sales-tax items and VTYPE vendors; not written here",
                               "no link between a payment and the invoices it pays",
                               "no duplicate prevention on import"]
        base.write_status = "implemented for customers, untaxed invoices and credit memos, payments and payouts as deposits; refunds have no example in the kit and are not written"
    end if
    return finio.check_registry_entry(base)
end function

end library
