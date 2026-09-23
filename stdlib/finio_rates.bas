' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `finio_rates` -- reference and benchmark rates, pulled from the institutions
' that publish them.
'
' WHY THIS IS IN THE finio FAMILY. finio's subject is that A FORMAT IS ONE THING
' AND ITS INTERPRETATION IS ANOTHER, and that provenance is an axiom rather than
' a debugging aid, because in this domain "where did this number come from" is a
' REGULATORY question. A benchmark rate is the same problem arriving over HTTP
' instead of in a file: a bank that priced a loan off SOFR on a Tuesday must be
' able to say, years later, which published value it used and where it got it.
'
' WHAT IT IS NOT. Not market data -- `market` covers daily price history and
' says honestly in its own header that free providers are unreliable. These
' feeds are different in kind: published by central banks and treasuries as a
' public duty, free, keyless, and institutionally committed to staying up.
'
' ===========================================================================
' A RATE IS DECIMAL TEXT, NOT A NUMBER, AND THAT IS MEASURED
' ===========================================================================
'
' The two keyless feeds disagree about representation, measured 2026-09-22:
'
'   Treasury FiscalData   "avg_interest_rate_amt": "3.788"     <- STRING
'   NY Fed                "percentRate": 3.85                  <- FLOAT
'
' So `rate` is kept as THE SOURCE'S OWN TEXT and `value` is the number beside
' it. This is the discipline `money.rate` already applies to FX -- eight
' significant figures is ordinary and a double rounds them -- and it matters
' more here than it looks: a basis point is 0.0001, a hundred million dollars
' of notional makes one basis point ten thousand dollars, and a rate that
' arrived as text and left as a double has lost digits nobody can recover.
'
' A CONSUMER THAT WANTS ARITHMETIC USES `value`. One that must reproduce what
' was published, or hand it to an auditor, uses `rate`.
library finio_rates

load finio

' ---------------------------------------------------------------------------
' What can be fetched
' ---------------------------------------------------------------------------

' KEYLESS ONLY, deliberately. A source needing an account is a source that stops
' working when somebody's key expires, in a library whose whole promise is that
' a number can be reproduced years later. FRED (~800,000 series) is excellent
' and needs a key; it belongs behind an explicit `finio_rates.keyed(...)` that
' does not exist yet, so that "this needs a key" is a decision a caller makes
' rather than a surprise at runtime.
function sources()
    return [
      { id: "nyfed_sofr",
        name: "SOFR -- Secured Overnight Financing Rate",
        publisher: "Federal Reserve Bank of New York",
        keyless: true,
        url: "https://markets.newyorkfed.org/api/rates/secured/sofr/last/{n}.json",
        revisions: true },
      { id: "nyfed_effr",
        name: "EFFR -- Effective Federal Funds Rate",
        publisher: "Federal Reserve Bank of New York",
        keyless: true,
        url: "https://markets.newyorkfed.org/api/rates/unsecured/effr/last/{n}.json",
        revisions: true },
      { id: "nyfed_obfr",
        name: "OBFR -- Overnight Bank Funding Rate",
        publisher: "Federal Reserve Bank of New York",
        keyless: true,
        url: "https://markets.newyorkfed.org/api/rates/unsecured/obfr/last/{n}.json",
        revisions: true },
      { id: "treasury_avg_interest",
        name: "Average interest rates on US Treasury securities",
        publisher: "US Treasury (FiscalData)",
        keyless: true,
        url: "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/avg_interest_rates",
        revisions: false }
    ]
end function

function source_ids()
    out = []
    for each s in sources()
        append(out, s.id)
    end for
    return out
end function

function source(id)
    for each s in sources()
        if s.id = id then
            return s
        end if
    end for
    error ("finio_rates: unknown source '" + string(id) + "' (known: "
           + join(source_ids(), ", ") + ")")
end function

' ---------------------------------------------------------------------------
' Options and the offline seam
' ---------------------------------------------------------------------------

function default_options()
    return { limit: 30, offline_dir: nothing, timeout: 30 }
end function

function _options(given)
    o = default_options()
    if is_nothing(given) then
        return o
    end if
    if not (type(given) = "record") then
        error "finio_rates: options must be a record"
    end if
    known = keys(o)
    for each k in keys(given)
        if not contains(known, k) then
            error ("finio_rates: unknown option '" + k + "' (known: "
                   + join(known, ", ") + ")")
        end if
        o[k] = given[k]
    end for
    return o
end function

' THE OFFLINE SEAM, the same one `llm` and `market` already use. A gate that
' reaches a central bank over the network is a gate that goes red when a
' publisher has an outage, for a reason that is not about gBASIC -- and it
' cannot assert a VALUE at all, because tomorrow's SOFR is not today's.
function offline(dir)
    return { offline_dir: dir }
end function

function _fetch_text(url, o, cache_name)
    if not is_nothing(o.offline_dir) then
        f {file}= string(o.offline_dir) + "/" + cache_name + ".json"
        if not exists(f) then
            error ("finio_rates: no recorded response for " + cache_name
                   + " (expected " + string(o.offline_dir) + "/" + cache_name
                   + ".json)")
        end if
        return read(f)
    end if
    r = webclient.get(url, { timeout: o.timeout })
    if r.status != 200 then
        error ("finio_rates: " + url + " answered " + string(r.status))
    end if
    return r.body
end function

' ---------------------------------------------------------------------------
' Fetching
' ---------------------------------------------------------------------------

' `finio_rates.fetch(source_id [, options])` -> an observation series.
'
' { ok, source, publisher, fetched_at, observations, why }
'
' Each observation:
'   { date, rate, value, revised, location }
'
' `rate` is the publisher's own text. `value` is that as a number, for
' arithmetic. `revised` is `unknown` where the feed does not say -- NOT `false`,
' because "this publisher does not report revisions" and "this value has not
' been revised" are different claims and a consumer pricing off the second one
' deserves to know which it has.
function fetch(source_id, options = nothing)
    o = _options(options)
    s = source(source_id)

    url = replace(s.url, "{n}", string(o.limit))
    body = _fetch_text(url, o, source_id)

    parsed = try_decode(body)
    if not parsed.ok then
        return { ok: false, source: s.id, publisher: s.publisher,
                 observations: [],
                 why: "the publisher's response was not JSON: " + parsed.message }
    end if

    obs = []
    if starts_with(s.id, "nyfed_") then
        obs = _nyfed_observations(parsed.value, s)
    else
        obs = _treasury_observations(parsed.value, s)
    end if

    return { ok: true,
             source: s.id,
             publisher: s.publisher,
             fetched_at: now(),
             observations: obs }
end function

' THE NY FED REPORTS ITS OWN REVISIONS, in `revisionIndicator`, and that field
' is why this feed is preferred where a choice exists: a rate that was corrected
' after publication is exactly the thing a bank reconciling a past period must
' not miss. Measured over 90 days on 2026-09-22 the field was empty throughout,
' which says the mechanism is quiet rather than absent.
function _nyfed_observations(doc, s)
    out = []
    if not has(doc, "refRates") then
        return out
    end if
    i = 0
    for each r in doc.refRates
        rev = unknown
        if has(r, "revisionIndicator") then
            rev = not (trim(string(r.revisionIndicator)) = "")
        end if
        ' THE PUBLISHED TEXT IS RECOVERED FROM THE NUMBER, because this feed
        ' sends a JSON float and there is no text to keep. `string()` renders
        ' the shortest decimal that reads back as the same double (PLAT-NUMFMT),
        ' so nothing is invented and nothing is lost that the feed still had.
        v = unknown
        txt = unknown
        if has(r, "percentRate") and not is_nothing(r.percentRate) then
            v = r.percentRate
            txt = string(v)
        end if
        append(out, { date: string(r.effectiveDate),
                      rate: txt,
                      value: v,
                      revised: rev,
                      ' A JSON POINTER, which is what finio.location's json kind takes -- an
                      ' RFC 6901 path into the response, so provenance names the
                      ' exact element rather than a row number that means nothing
                      ' once the response is re-fetched with a different limit.
                      location: finio.location("json",
                                 { json_pointer: "/refRates/" + string(i) }) })
        i = i + 1
    end for
    return out
end function

' TREASURY SENDS DECIMAL TEXT, so the text is kept as it arrived and the number
' is derived from it -- the direction that loses nothing.
function _treasury_observations(doc, s)
    out = []
    if not has(doc, "data") then
        return out
    end if
    i = 0
    for each r in doc.data
        txt = unknown
        v = unknown
        if has(r, "avg_interest_rate_amt") then
            txt = trim(string(r.avg_interest_rate_amt))
            if len(txt) > 0 then
                v = number(txt)
            else
                txt = unknown
            end if
        end if
        append(out, { date: string(r.record_date),
                      rate: txt,
                      value: v,
                      ' THIS PUBLISHER DOES NOT REPORT REVISIONS, so the answer
                      ' is `unknown` rather than `false`. Axiom 7: an absent
                      ' answer and a negative answer are different answers.
                      revised: unknown,
                      security: string(r.security_desc),
                      location: finio.location("json",
                                 { json_pointer: "/data/" + string(i) }) })
        i = i + 1
    end for
    return out
end function

' ---------------------------------------------------------------------------
' Reading a series
' ---------------------------------------------------------------------------

' The rate EFFECTIVE on a date: the latest observation on or before it.
'
' The same rule `money.rate_on` follows for FX, and for the same reason -- a
' report run for March must see March's rate rather than today's, or the number
' cannot be reproduced. Answers `unknown` when the series does not reach back
' that far, NEVER the oldest value it happens to hold.
function on_date(series, d)
    want = string(d)
    best = unknown
    for each ob in series.observations
        if ob.date <= want then
            if is_unknown(best) then
                best = ob
            else
                if ob.date > best.date then
                    best = ob
                end if
            end if
        end if
    end for
    return best
end function

function latest(series)
    best = unknown
    for each ob in series.observations
        if is_unknown(best) or ob.date > best.date then
            best = ob
        end if
    end for
    return best
end function

' Observations the publisher has marked as revised. A bank reconciling a closed
' period asks this; a bank pricing tomorrow does not.
function revisions(series)
    out = []
    for each ob in series.observations
        if not is_unknown(ob.revised) then
            if ob.revised then
                append(out, ob)
            end if
        end if
    end for
    return out
end function

end library
