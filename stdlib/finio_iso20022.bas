' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_iso20022 -- the mechanics every ISO 20022 adapter needs.
'
' EXTRACTED AT THE SECOND CALLER, not the first. `finio_camt` had all of this
' privately and that was right while it was alone; `finio_pain001` needs the
' same namespace-version rule, the same Ccy-attribute amount and the same
' absent-versus-invalid field distinction, and two copies of one rule is the
' drift this tree keeps finding. The same argument said NOT to extract
' `finio_ofx`'s tag scanner, which still has one caller.
'
' WHAT IS SHARED IS WHAT THE STANDARD DEFINES, not what a message means. The
' version lives in the namespace for every ISO 20022 message; an amount carries
' its currency in a `Ccy` ATTRIBUTE; an absent element is `unknown` and a
' present one that cannot be what it claims is `invalid`. Which elements exist
' and what they add up to belongs to each message's own adapter.

library finio_iso20022

load finio from "finio.bas"
load xml

function namespace_prefix()
    return "urn:iso:std:iso:20022:tech:xsd:"
end function

' The version a document declares, e.g. "camt.053.001.08" or "pain.001.001.03".
' NO PARSE: recognition runs over every file in an archive sweep, and parsing
' each one to discover it is a purchase order would cost the whole scan.
function declared_message(text)
    at = byte_find(text, namespace_prefix(), 0)
    if is_nothing(at) then
        return ""
    end if
    i = at + byte_count(namespace_prefix())
    n = byte_count(text)
    out = ""
    while i < n
        c = byte_slice(text, i, 1)
        if (c >= "a" and c <= "z") or (c >= "0" and c <= "9") or c = "." then
            out = out + c
            i = i + 1
        else
            i = n
        end if
    end while
    return out
end function

' Does a declared message name belong to a family, e.g. "pain.001"?
function is_family(message, family)
    return byte_count(message) > byte_count(family) and byte_slice(message, 0, byte_count(family) + 1) = family + "."
end function

' AXIOM 7, and in XML the distinction has a shape it lacks in a fixed-width
' record: an ABSENT element is `unknown` -- the document said nothing, which for
' an optional element is ordinary and not a defect -- while a PRESENT element
' whose content cannot be what it claims is `invalid`. §18's rule that the token
' as written travels beside any mapped meaning is why `raw` is kept even where a
' typed value was produced.
function text_field(node, path, full_path, occurrence)
    n = xml.find(node, path)
    loc = finio.location("xml", { path: full_path, occurrence: occurrence })
    if is_unknown(n) then
        return { status: "unknown", value: unknown, raw: unknown, location: loc }
    end if
    t = xml.text(n)
    if byte_count(trim(t)) = 0 then
        return { status: "unknown", value: unknown, raw: t, location: loc }
    end if
    return { status: "ok", value: trim(t), raw: t, location: loc }
end function

function number_field(node, path, full_path, occurrence)
    f = text_field(node, path, full_path, occurrence)
    if f.status != "ok" then
        return f
    end if
    if not all_digits(f.value) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 why: "expected a count, found '" + f.value + "'" }
    end if
    return { status: "ok", value: number(f.value), raw: f.raw, location: f.location }
end function

' THE CURRENCY IS AN ATTRIBUTE, which is a shape a fixed-width format has no
' equivalent for: the amount and the unit it is denominated in live in different
' places in the document, and reading the number without the attribute yields a
' perfectly ordinary figure in no particular currency.
function amount_field(node, path, full_path, occurrence)
    n = xml.find(node, path)
    loc = finio.location("xml", { path: full_path, occurrence: occurrence })
    if is_unknown(n) then
        return { status: "unknown", value: unknown, raw: unknown, location: loc, currency: unknown }
    end if
    t = trim(xml.text(n))
    ccy = xml.attr(n, "Ccy", "")
    if byte_count(t) = 0 then
        return { status: "unknown", value: unknown, raw: t, location: loc, currency: unknown }
    end if
    if byte_count(ccy) = 0 then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: unknown,
                 why: "the amount carries no Ccy attribute, so it is a number in no currency" }
    end if
    if not is_decimal(t) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + t + "' is not a decimal amount" }
    end if
    m = money_of(ccy, t)
    if is_unknown(m) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + ccy + "' is not a currency this build knows" }
    end if
    return { status: "ok", value: m, raw: t, location: loc, currency: ccy }
end function

' A DECIMAL WITH NO CURRENCY OF ITS OWN, which several ISO 20022 elements are:
' a control sum states a total in the currency its context declares, and
' reading it with the amount rule above reports every well-formed one as
' unreadable.
function decimal_field(node, path, full_path, occurrence, ccy)
    f = text_field(node, path, full_path, occurrence)
    if f.status != "ok" then
        return f
    end if
    if not is_decimal(f.value) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 currency: ccy, why: "'" + f.value + "' is not a decimal amount" }
    end if
    if byte_count(ccy) = 0 then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 currency: unknown,
                 why: "this total has no currency of its own and its context declares none" }
    end if
    m = money_of(ccy, f.value)
    if is_unknown(m) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 currency: ccy, why: "'" + ccy + "' is not a currency this build knows" }
    end if
    return { status: "ok", value: m, raw: f.raw, location: f.location, currency: ccy }
end function

function money_of(ccy, text)
    on error goto next
    m = money.of(ccy, text)
    if error then
        error.clear()
        return unknown
    end if
    return m
end function

function is_decimal(s)
    n = byte_count(s)
    if n = 0 then
        return false
    end if
    i = 0
    dots = 0
    digits = 0
    while i < n
        c = byte_slice(s, i, 1)
        if c = "." then
            dots = dots + 1
        else
            if c = "-" and i = 0 then
                i = i
            else
                if c < "0" or c > "9" then
                    return false
                end if
                digits = digits + 1
            end if
        end if
        i = i + 1
    end while
    return dots <= 1 and digits > 0
end function

function all_digits(s)
    n = byte_count(s)
    if n = 0 then
        return false
    end if
    i = 0
    while i < n
        c = byte_slice(s, i, 1)
        if c < "0" or c > "9" then
            return false
        end if
        i = i + 1
    end while
    return true
end function
end library
