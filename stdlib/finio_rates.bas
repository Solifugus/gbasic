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

' THE TRANSPORT, loaded inside the block as a library's dependencies must be.
' Missing until 2026-11-03: the offline replay never reaches it, so every
' tier passed while a live fetch failed at its first call with "library not
' loaded: webclient" -- found by the first run of the live check.
load webclient

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
        revisions: false },
      ' EXCHANGE RATES, added 2026-11-03. The ECB's euro reference rates: about
      ' thirty currencies, one fix a day at 14:15 CET, history to 1999. Fetched
      ' by DATE WINDOW, never "the last N": measured, lastNObservations hands
      ' back a series' last N whenever they were, so RUB answered with March
      ' 2022 and the Greek drachma with December 2000 -- a stale rate looking
      ' exactly like today's. CSV rather than XML so it needs no libxml2.
      { id: "ecb_fx",
        name: "ECB euro foreign exchange reference rates",
        publisher: "European Central Bank",
        keyless: true,
        kind: "fx",
        url: "https://data-api.ecb.europa.eu/service/data/EXR/D..EUR.SP00.A?startPeriod={from}&endPeriod={to}&format=csvdata",
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

' `from` and `to` are the window an exchange-rate source is fetched for; with
' neither, it is the last `limit` days. The interest-rate sources ignore them.
function default_options()
    return { limit: 30, offline_dir: nothing, timeout: 30, from: nothing, to: nothing }
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

function _fetch_text(url, o, cache_name, extension = "json")
    if not is_nothing(o.offline_dir) then
        f {file}= string(o.offline_dir) + "/" + cache_name + "." + extension
        if not exists(f) then
            error ("finio_rates: no recorded response for " + cache_name
                   + " (expected " + string(o.offline_dir) + "/" + cache_name
                   + "." + extension + ")")
        end if
        return read(f)
    end if
    r = webclient.request({ method: "GET", url: url, timeout: o.timeout })
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
    if has(s, "kind") then
        if s.kind = "fx" then
            return _fetch_fx(s, o)
        end if
    end if

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
' Exchange rates
' ---------------------------------------------------------------------------

' A RATE OLDER THAN THIS IS NOT THE RATE FOR A DAY. The ECB does not publish on
' weekends or on its closing days; measured over the 2025 year end, the longest
' gap was five days (24 to 29 December). A week covers every gap the calendar
' makes, and a series that has stopped -- the lev when Bulgaria joined the
' euro, the rouble when it was suspended -- falls out of it within days rather
' than answering for years with its last value.
function default_fx_options()
    return { max_age: 7 days }
end function

function _fx_options(given)
    o = default_fx_options()
    if is_nothing(given) then
        return o
    end if
    for each k in keys(given)
        if not contains(keys(o), k) then
            error ("finio_rates: unknown fx option '" + k + "' (known: "
                   + join(keys(o), ", ") + ")")
        end if
        o[k] = given[k]
    end for
    return o
end function

function _fx_window(o)
    last = o.to
    if is_nothing(last) then
        last {date}= now()
    else
        last {date}= last
    end if
    first = o.from
    if is_nothing(first) then
        first = last - (o.limit * 1 day)
    else
        first {date}= first
    end if
    return { from: first, to: last }
end function

' The ECB series. Each observation is a PUBLISHED rate: `base` is EUR and the
' rate is units of `quote` per euro, as the ECB states it ("US dollar/Euro",
' 1.1179). Series that are not reference rates -- four "indicative" series the
' ECB stopped in 2020 -- are left out and named in `excluded`, not dropped
' silently.
function _fetch_fx(s, o)
    w = _fx_window(o)
    url = replace(replace(s.url, "{from}", string(w.from)), "{to}", string(w.to))
    body = _fetch_text(url, o, s.id, "csv")
    obs = []
    excluded = []
    for each r, at in csv.parse(body)
        if not has(r, "OBS_VALUE") or not has(r, "CURRENCY") then
            return { ok: false, source: s.id, publisher: s.publisher, kind: "fx",
                     observations: [], excluded: [],
                     why: "the publisher's response has no CURRENCY and OBS_VALUE columns" }
        end if
        if r.OBS_COM = "Indicative rate" then
            if not contains(excluded, r.CURRENCY) then
                append(excluded, r.CURRENCY)
            end if
            continue
        end if
        txt = trim(r.OBS_VALUE)
        if len(txt) = 0 then
            continue
        end if
        append(obs, { date: r.TIME_PERIOD,
                      rate: txt,
                      value: number(txt),
                      base: r.CURRENCY_DENOM,
                      quote: r.CURRENCY,
                      derived: false,
                      ' THIS PUBLISHER DOES NOT REPORT REVISIONS, so `unknown`.
                      revised: unknown,
                      location: finio.location("delimited", { row: at.line, column: "OBS_VALUE" }) })
    end for
    return { ok: true, source: s.id, publisher: s.publisher, kind: "fx",
             base: "EUR", from: w.from, to: w.to, fetched_at: now(),
             observations: obs, excluded: excluded }
end function

' The published observation of `quote` per euro ON a publication date, or
' unknown.
function _fx_published(series, quote, day)
    for each ob in series.observations
        if ob.quote = quote and ob.date = day then
            return ob
        end if
    end for
    return unknown
end function

' A DERIVED RATE is one division of two published ones, written to ten
' significant figures. The published rates carry four to six, so ten keeps
' every digit the inputs can support and none they cannot -- where the double's
' own seventeen would print noise as if it were a fact.
function _derived_text(x)
    places = 9 - floor(log10(x))
    if places < 0 then
        places = 0
    end if
    return string(round(x, places))
end function

' The rate from `from` to `to` on ONE publication date, published or derived.
' Answers unknown when the date does not carry what is needed.
function _fx_on_day(series, from_ccy, to_ccy, day)
    if from_ccy = to_ccy then
        return { from: from_ccy, to: to_ccy, date: day, rate: "1", value: 1,
                 derived: false, via: [] }
    end if
    if from_ccy = series.base then
        ob = _fx_published(series, to_ccy, day)
        if is_unknown(ob) then
            return unknown
        end if
        return { from: from_ccy, to: to_ccy, date: day, rate: ob.rate, value: ob.value,
                 derived: false, via: [ob] }
    end if
    if to_ccy = series.base then
        ob = _fx_published(series, from_ccy, day)
        if is_unknown(ob) then
            return unknown
        end if
        txt = _derived_text(1 / ob.value)
        return { from: from_ccy, to: to_ccy, date: day, rate: txt, value: number(txt),
                 derived: true, how: "the inverse of " + series.base + "/" + from_ccy,
                 via: [ob] }
    end if
    a = _fx_published(series, from_ccy, day)
    b = _fx_published(series, to_ccy, day)
    if is_unknown(a) or is_unknown(b) then
        return unknown
    end if
    txt = _derived_text(b.value / a.value)
    return { from: from_ccy, to: to_ccy, date: day, rate: txt, value: number(txt),
             derived: true,
             how: "a cross through " + series.base + ": " + to_ccy + " per " + series.base
                  + " over " + from_ccy + " per " + series.base,
             via: [a, b] }
end function

function _fx_dates(series)
    days = []
    for each ob in series.observations
        if not contains(days, ob.date) then
            append(days, ob.date)
        end if
    end for
    return sort(days)
end function

' `finio_rates.fx_on(series, from, to, d [, options])` -> the rate effective on
' `d`: the latest publication on or before it that carries both currencies, and
' no older than `max_age` (a week by default). Answers unknown otherwise --
' never a rate from a series that has stopped.
'
' { from, to, on, date, age, rate, value, derived, how, via }
'
' `derived` is false for a rate the ECB published (EUR to a currency) and true
' for one computed from published rates: the inverse (a currency to EUR) or a
' cross (USD to GBP, through the euro). `how` says which, and `via` holds the
' published observations it came from, each with its location in the response.
' Deriving is sound for reference rates because a reference rate is a single
' middle rate with no buy/sell spread; it is NOT sound for a rate a program
' records with money.rate, which is why money.rate still refuses to invert.
function fx_on(series, from_ccy, to_ccy, d, options = nothing)
    o = _fx_options(options)
    want {date}= d
    best = unknown
    for each day in _fx_dates(series)
        if day <= string(want) then
            r = _fx_on_day(series, from_ccy, to_ccy, day)
            if not is_unknown(r) then
                best = r
            end if
        end if
    end for
    if is_unknown(best) then
        return unknown
    end if
    published {date}= best.date
    age = want - published
    if age > o.max_age then
        return unknown
    end if
    best.on = want
    best.age = age
    return best
end function

' `finio_rates.load_fx(series, pairs)` records each pair's rate for every
' publication date in the series with money.rate, so money.convert can use it.
' `pairs` is a list of { from, to } records. Answers what it did:
'
' { loaded, pairs: [{ from, to, derived, how, dates }], skipped: [{ from, to, why }] }
'
' A pair is skipped, and named with its reason, when gBASIC's money does not
' know a currency (the lev left ISO 4217 when Bulgaria joined the euro) or the
' series never carries it. Nothing is skipped silently.
'
' money.convert uses the latest rate on or before a date with no age limit, so
' a window loaded in January answers for June with January's rate. Load the
' window the conversions are for; fx_on is the lookup that refuses a stale rate.
function load_fx(series, pairs)
    loaded = 0
    done = []
    skipped = []
    for each p in pairs
        dates = 0
        derived = false
        how = ""
        why = ""
        for each day in _fx_dates(series)
            r = _fx_on_day(series, p.from, p.to, day)
            if is_unknown(r) then
                continue
            end if
            why = _record_rate(p.from, p.to, r.rate, day)
            if len(why) > 0 then
                break
            end if
            dates = dates + 1
            derived = r.derived
            if r.derived then
                how = r.how
            end if
        end for
        if len(why) > 0 then
            append(skipped, { from: p.from, to: p.to, why: why })
        else if dates = 0 then
            append(skipped, { from: p.from, to: p.to,
                              why: "the series carries no publication with both currencies" })
        else
            loaded = loaded + dates
            append(done, { from: p.from, to: p.to, derived: derived, how: how, dates: dates })
        end if
    end for
    return { loaded: loaded, pairs: done, skipped: skipped }
end function

' money.rate, with its refusal returned as text rather than raised, so one
' currency gBASIC does not know does not stop the others loading.
function _record_rate(from_ccy, to_ccy, rate, day)
    on error goto refused
    d {date}= day
    money.rate(from_ccy, to_ccy, rate, d)
    return ""
refused:
    m = error.message
    error.clear()
    return m
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
