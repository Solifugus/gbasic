' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' market.bas — daily price history, as a frame.
'
' THE GAP THIS FILLS. gBASIC's finance stack was complete except for its input.
' `stats.simple_returns(prices)`, `sharpe_ratio`, `max_drawdown`,
' `value_at_risk`, `capm` and `forensics.altman_classic(facts, prices)` every
' one take prices as an ARGUMENT, and nothing in the tree produced them: EDGAR
' serves filings, not quotes. So the whole return-based half of the library was
' unreachable without assembling a price series by hand. This is that producer.
'
'   load market
'
'   from_d{date}= "2024-01-01"
'   to_d{date}= "2024-12-31"
'
'   m = market.stooq()
'   r = market.daily(m, "AAPL", from_d, to_d)
'   if r.ok then
'       rets = stats.simple_returns(r.frame["close"])
'   end if
'
' The result is a FRAME (columns: date, open, high, low, close, volume), which
' is the shape both consumers already want: `forensics` indexes it by column
' (`prices["date"]`, `prices["close"]`) and `stats` takes `frame["close"]`,
' which is exactly the flat array of numbers it expects.
'
' TWO SILENT WRONG ANSWERS THIS REFUSES TO PRODUCE. Both are the kind that
' yield a plausible number rather than an error, which is why they are handled
' here rather than left to the caller:
'
'   * ORDER. Providers disagree about whether history runs oldest-first or
'     newest-first, and `simple_returns` on a reversed series returns the
'     NEGATED sequence -- a perfectly ordinary-looking set of returns with the
'     sign wrong. Rows are always sorted ascending by date before returning,
'     whatever the provider sent.
'   * ADJUSTMENT. A 2-for-1 split halves the raw close overnight, and a return
'     computed from unadjusted prices reads it as a -50% day. Adjustment is
'     therefore never guessed at: the result record carries `adjusted`, set
'     from what the PROVIDER actually supplies, and a provider that serves raw
'     prices says so rather than having its close silently relabelled.
'
' Failure is a VALUE, not a raise: a symbol that does not exist and a network
' that is down are ordinary outcomes for this library, not bugs in the caller.
' `daily` returns `{ ok, frame, adjusted, message }` -- the shape `try_decode`,
' `xlsx.try_open` and `web.pool` already established.
'
' Tests never touch the network. `market.offline(m, dir)` replays committed
' fixtures and `market.with_transport(m, fn)` injects a function, the two seams
' `llm.bas` and `edgar.bas` both use.
'
' PROVIDER REALITY, checked live 2026-08-25 rather than assumed: keyless daily
' equity data has largely disappeared. Stooq serves a JavaScript anti-bot
' challenge to any HTTP client, and Yahoo's chart endpoint answered 429. A
' provider with an API KEY is the reliable path; the keyless ones are kept
' because the shape is right and endpoints change, and because `daily` now
' names a challenge page or a rate limit for what it is instead of reporting
' "no rows" and sending you to look for a bad symbol.
library market

    ' A library's dependencies belong INSIDE its block: a top-level `load` does
    ' not put them in scope for the library's own functions (docs/reference.md,
    ' "A library's own dependencies are declared INSIDE its `library` block").
    ' Written the wrong way here at first, and the offline tests could never
    ' catch it -- they never reach the transport, so `webclient` was never
    ' called. Only the live path fails, and it fails at the first real fetch.
    load webclient

    ' --- providers ----------------------------------------------------------

    function _handle(name, adjusted, key)
        return {
            provider: name,
            adjusted: adjusted,
            key: key,
            timeout: 30,
            offline_dir: unknown,
            transport: unknown
        }
    end function

    ' Stooq: daily OHLCV as CSV, no API key.
    '
    ' *** CHECKED LIVE 2026-08-25 AND CURRENTLY UNUSABLE. *** Stooq now answers
    ' an unauthenticated client with a JavaScript proof-of-work interstitial --
    ' HTTP 200, an HTML body, no data, regardless of user-agent. It needs a real
    ' browser, so no HTTP client can reach it. Kept because the endpoint may
    ' come back and the shape is right; `daily` names the challenge explicitly
    ' rather than reporting an empty result. For working keyless data the
    ' honest answer today is: there is not much. Use a keyed provider.
    '
    ' Declared UNADJUSTED because that is what the endpoint serves. Saying so
    ' is the whole point: a caller measuring returns across a split needs to
    ' know, and a library that quietly labelled this "adjusted" would hand them
    ' a -50% day and no way to notice.
    function stooq()
        return _handle("stooq", false, "")
    end function

    ' Tiingo: API key required, split- and dividend-adjusted daily prices.
    function tiingo(key)
        return _handle("tiingo", true, key)
    end function

    ' --- injection seams (data in, updated handle out) ----------------------

    function offline(m, dir)
        m.offline_dir = dir
        return m
    end function

    function with_transport(m, fn)
        m.transport = fn
        return m
    end function

    function with_timeout(m, seconds)
        m.timeout = seconds
        return m
    end function

    ' --- transport (injected > offline fixture > real http) -----------------

    function _http_transport(m, req)
        resp = webclient.request({ method: "GET", url: req.url, timeout: m.timeout })
        return { status: resp.status, body: resp.body }
    end function

    ' One fixture per provider+symbol, named so a directory of them is
    ' readable: `stooq_AAPL.csv`.
    function _offline_transport(m, req)
        path = m.offline_dir + "/" + m.provider + "_" + req.symbol + "." + _wire(m)
        f{file}= path
        if not exists(f) then
            return { status: 404, body: "" }
        end if
        return { status: 200, body: join(read_lines(f), "\n") }
    end function

    function _transport_call(m, req)
        if not is_unknown(m.transport) then
            fn = m.transport
            return fn(m, req)
        end if
        if not is_unknown(m.offline_dir) then
            return _offline_transport(m, req)
        end if
        return _http_transport(m, req)
    end function

    function _wire(m)
        if m.provider = "tiingo" then
            return "json"
        end if
        return "csv"
    end function

    ' --- urls ---------------------------------------------------------------

    ' A date value already renders ISO, so this is only a name for intent.
    function _iso(d)
        return string(d)
    end function

    function _url(m, symbol, from_date, to_date)
        if m.provider = "stooq" then
            ' Stooq wants an exchange suffix; US listings are `.us`.
            s = lower(symbol)
            if not contains(s, ".") then
                s = s + ".us"
            end if
            return "https://stooq.com/q/d/l/?s=" + s + "&d1=" + replace(_iso(from_date), "-", "") + "&d2=" + replace(_iso(to_date), "-", "") + "&i=d"
        end if
        if m.provider = "tiingo" then
            return "https://api.tiingo.com/tiingo/daily/" + lower(symbol) + "/prices?startDate=" + _iso(from_date) + "&endDate=" + _iso(to_date) + "&token=" + m.key
        end if
        error "market: unknown provider '" + string(m.provider) + "'"
    end function

    ' --- parsing ------------------------------------------------------------

    function _looks_like_html(body)
        head = lower(trim(left(string(body), 400)))
        if starts_with(head, "<!doctype") or starts_with(head, "<html") then
            return true
        end if
        return false
    end function

    function _challenge_hint(body)
        low = lower(string(body))
        if contains(low, "javascript") or contains(low, "verify your browser") then
            return " (an anti-bot challenge that needs a real browser -- this provider is not usable from an HTTP client; use one with an API key)"
        end if
        return " (it may require an API key)"
    end function

    function _fail(message)
        return { ok: false, frame: unknown, adjusted: false, message: message }
    end function

    ' A CSV body -> array of records. frame.read_csv reads a PATH; a provider
    ' hands us a string, so the header/row split lives here.
    function _rows_from_csv(body)
        lines = split(body, "\n")
        if count(lines) < 2 then
            return []
        end if
        head = split(trim(lines[0]), ",")
        names = []
        i = 0
        while i < count(head)
            append(names, lower(trim(head[i])))
            i = i + 1
        end while

        rows = []
        r = 1
        while r < count(lines)
            line = trim(lines[r])
            if len(line) > 0 then
                cells = split(line, ",")
                if count(cells) = count(names) then
                    rec = {}
                    c = 0
                    while c < count(names)
                        rec[names[c]] = trim(cells[c])
                        c = c + 1
                    end while
                    append(rows, rec)
                end if
            end if
            r = r + 1
        end while
        return rows
    end function

    ' Tiingo serves a JSON array of objects, not CSV. Parsed with `try_decode`
    ' rather than `decode` because a provider can return anything at all --
    ' an error page, a truncated body -- and that is an ordinary outcome here,
    ' not a bug in the caller.
    function _rows_from_json(body)
        parsed = try_decode(body)
        if not parsed.ok then
            return []
        end if
        if not is_array(parsed.value) then
            return []
        end if
        ' Key case is NORMALISED here so the two wire formats converge on one
        ' vocabulary. `decode` preserves JSON casing, so Tiingo's field is
        ' `adjClose`, while the CSV path already lowercases its header --
        ' without this, a lookup that works for one provider silently misses
        ' for the other, and a missed adjusted-close is exactly the wrong
        ' answer this library is built to refuse.
        out = []
        for each row in parsed.value
            if is_record(row) then
                flat = {}
                for each k in keys(row)
                    flat[lower(k)] = row[k]
                next k
                append(out, flat)
            end if
        next row
        return out
    end function

    ' A provider may send a full ISO timestamp ("2024-01-02T00:00:00.000Z");
    ' the date modifier takes the date part only and refuses the rest, so trim
    ' at the T rather than hand it something it will reject.
    function _date_part(text)
        t = trim(string(text))
        cut = find(t, "T")
        if not is_nothing(cut) then
            t = left(t, cut)
        end if
        return t
    end function

    ' The two wire formats disagree about type as well as about case: CSV
    ' hands back strings, `decode` hands back real numbers (and `nothing` for
    ' JSON null). Both converge here rather than at each call site.
    function _num_or_unknown(text)
        if is_unknown(text) or is_nothing(text) then
            return unknown
        end if
        if is_number(text) then
            return text
        end if
        if not is_string(text) then
            return unknown
        end if
        t = trim(text)
        if len(t) = 0 then
            return unknown
        end if
        if t = "null" or t = "N/A" then
            return unknown
        end if
        return number(t)
    end function

    ' --- the verb -----------------------------------------------------------

    ' Daily history for one symbol, inclusive of both endpoints.
    '
    '   { ok: true,  frame: <date open high low close volume>, adjusted: bool,
    '     message: "" }
    '   { ok: false, frame: unknown, adjusted: false, message: "why" }
    function daily(m, symbol, from_date, to_date)
        if not is_string(symbol) or len(trim(symbol)) = 0 then
            return _fail("market.daily needs a non-empty symbol")
        end if
        if from_date > to_date then
            return _fail("market.daily: from_date is after to_date")
        end if

        req = { url: _url(m, symbol, from_date, to_date), symbol: upper(trim(symbol)) }
        resp = _transport_call(m, req)

        if resp.status = 404 then
            return _fail("market: no data for symbol '" + symbol + "' from " + m.provider)
        end if
        if resp.status = 429 then
            return _fail("market: " + m.provider + " rate-limited this request (HTTP 429) -- wait, or use a provider with an API key")
        end if
        if resp.status != 200 then
            return _fail("market: " + m.provider + " returned status " + string(resp.status))
        end if

        ' A 200 that is a WEB PAGE, not data. Providers increasingly answer an
        ' unauthenticated client with an anti-bot interstitial -- Stooq now
        ' serves a JavaScript proof-of-work challenge -- and it arrives as a
        ' perfectly successful 200. Parsed as CSV that yields nothing, and the
        ' old message said "returned no rows", which sends the caller looking
        ' for a bad symbol or a bad date range. Name what actually happened.
        if _looks_like_html(resp.body) then
            return _fail("market: " + m.provider + " returned a web page rather than data" + _challenge_hint(resp.body))
        end if

        if _wire(m) = "json" then
            rows = _rows_from_json(resp.body)
        else
            rows = _rows_from_csv(resp.body)
        end if
        if count(rows) = 0 then
            return _fail("market: " + m.provider + " returned no rows for '" + symbol + "'")
        end if

        ' Column names differ by provider; map onto one vocabulary.
        dates = []
        opens = []
        highs = []
        lows = []
        closes = []
        vols = []
        for each row in rows
            if has(row, "date") then
                d{date}= _date_part(row.date)
                append(dates, d)
                append(opens,  _num_or_unknown(_pick(row, "open")))
                append(highs,  _num_or_unknown(_pick(row, "high")))
                append(lows,   _num_or_unknown(_pick(row, "low")))
                ' An adjusted provider serves BOTH: `close` is the raw
                ' print and `adjClose` is split/dividend adjusted. Reporting
                ' adjusted:true while handing back the raw close would be the
                ' exact lie this library exists to avoid, so the adjusted
                ' column wins wherever the provider supplies one.
                if m.adjusted and has(row, "adjclose") then
                    append(closes, _num_or_unknown(_pick(row, "adjclose")))
                else
                    append(closes, _num_or_unknown(_pick(row, "close")))
                end if
                append(vols,   _num_or_unknown(_pick(row, "volume")))
            end if
        next row

        if count(dates) = 0 then
            return _fail("market: " + m.provider + " rows carried no date column")
        end if

        out = {
            date: dates, open: opens, high: highs,
            low: lows, close: closes, volume: vols
        }
        out = _sort_ascending(out)

        return { ok: true, frame: out, adjusted: m.adjusted, message: "" }
    end function

    function _pick(row, name)
        if has(row, name) then
            return row[name]
        end if
        return unknown
    end function

    ' Ascending by date, ALWAYS -- see the header. `simple_returns` on a
    ' newest-first series returns the negated sequence, which looks like
    ' ordinary data and is wrong in a way no type can catch.
    function _sort_ascending(f)
        n = count(f["date"])
        order = []
        i = 0
        while i < n
            append(order, i)
            i = i + 1
        end while

        ' Insertion sort on the index vector: history arrives nearly sorted
        ' (usually fully, in one direction or the other), which is the case
        ' this is linear on.
        i = 1
        while i < n
            k = order[i]
            kd = f["date"][k]
            j = i - 1
            while j >= 0 and f["date"][order[j]] > kd
                order[j + 1] = order[j]
                j = j - 1
            end while
            order[j + 1] = k
            i = i + 1
        end while

        out = {}
        for each col in keys(f)
            moved = []
            for each idx in order
                append(moved, f[col][idx])
            next idx
            out[col] = moved
        next col
        return out
    end function

    ' --- convenience --------------------------------------------------------

    ' Just the closing prices, which is what every stats verb actually wants.
    ' Returns `unknown` on failure so it composes in an expression; use
    ' `daily` when you need the reason.
    function closes(m, symbol, from_date, to_date)
        r = daily(m, symbol, from_date, to_date)
        if not r.ok then
            return unknown
        end if
        return r.frame["close"]
    end function

end library
