' SPDX-License-Identifier: AGPL-3.0-or-later
'
' Copyright (C) 2026 Matthew C. Tedder
'
' This file is part of gBASIC. Unlike the gBASIC interpreter and the rest of the
' standard library, which are Apache-2.0, THIS FILE IS LICENSED UNDER THE GNU
' AFFERO GENERAL PUBLIC LICENSE v3.0 OR LATER. See LICENSE.AGPL-3.0, and
' LICENSING.md for which files are under which licence and why.
'
' This program is free software: you can redistribute it and/or modify it under
' the terms of the GNU Affero General Public License as published by the Free
' Software Foundation, either version 3 of the License, or (at your option) any
' later version.
'
' This program is distributed in the hope that it will be useful, but WITHOUT
' ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
' FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
' details. You should have received a copy along with this program; if not, see
' <https://www.gnu.org/licenses/>.
'
' A COMMERCIAL LICENCE is available for use in proprietary or hosted products
' without the AGPL's obligations. Contact matthewct@gmail.com.

' edgar.bas — EDGAR acquisition core (edgar_design.md §4.1, WP-EDG-2).
'
' Owns transport, identity, rate limiting, and caching. Knows nothing about
' balance sheets — that is fundamentals.bas.
'
' STATE MODEL. gBASIC .bas libraries hold no mutable module/global state, so
' edgar threads an explicit SESSION HANDLE: edgar.session() makes one, the
' setters (identify/offline/cache) return an updated copy, and every call takes
' the handle as its first argument. The handle carries config only. Genuinely
' mutable runtime state — cached document bodies and the transport counter —
' lives in the sqlite cache DB, which is real on-disk persistence.
'
' Identifiers (CIK) are STRINGS everywhere. Missing data is `unknown`, never a
' guess (the standing NA policy).
library edgar
    load sqlite
    load webclient

    ' --- endpoints (edgar_design.md §2) ---
    function _tickers_url()
        return "https://www.sec.gov/files/company_tickers.json"
    end function
    function _submissions_base()
        return "https://data.sec.gov/submissions"
    end function
    function _companyfacts_base()
        return "https://data.sec.gov/api/xbrl/companyfacts"
    end function

    ' Short TTL (seconds) for index/facts endpoints; documents are immutable and
    ' bypass this (edgar_design.md §4.1).
    function _ttl_seconds()
        return 3600
    end function

    ' --- session handle -----------------------------------------------------
    function session()
        return { ident: unknown, offline_dir: unknown, cache_path: _default_cache_path() }
    end function

    function _default_cache_path()
        home = env("HOME")
        if is_unknown(home) then
            return "edgar_cache.db"
        end if
        return home + "/.gbasic/edgar.db"
    end function

    ' Setters — each returns the updated handle (data in, data out).
    function identify(e, contact)
        e.ident = contact
        return e
    end function

    function offline(e, dir)
        e.offline_dir = dir
        return e
    end function

    function cache(e, path)
        e.cache_path = path
        return e
    end function

    ' --- cache DB -----------------------------------------------------------
    function _parent_dir(path)
        parts = split(path, "/")
        if count(parts) <= 1 then
            return "."
        end if
        out = ""
        i = 0
        while i < count(parts) - 1
            if i > 0 then
                out = out + "/"
            end if
            out = out + parts[i]
            i = i + 1
        end while
        if out = "" then
            return "/"
        end if
        return out
    end function

    function _open_cache(e)
        parent = _parent_dir(e.cache_path)
        ' Create the cache directory if absent. exists() accepts only FILE
        ' references (not directory references), but a file reference to a
        ' directory path reports existence correctly, so probe with one.
        pcheck{file}= parent
        if not exists(pcheck) then
            mkref{dir}= parent
            make_dir(mkref)
        end if
        db = sqlite.connect(e.cache_path)
        sqlite.exec(db, "create table if not exists cache (url text primary key, body text not null, fetched_at integer not null, immutable integer not null)")
        sqlite.exec(db, "create table if not exists meta (k text primary key, v integer not null)")
        return db
    end function

    function _bump_transport(db)
        rows = sqlite.query(db, "select v from meta where k = 'transport'")
        n = 0
        if count(rows) > 0 then
            n = rows[0].v
        end if
        sqlite.exec(db, "insert or replace into meta (k, v) values ('transport', ?)", [n + 1])
    end function

    function transport_count(e)
        db = _open_cache(e)
        rows = sqlite.query(db, "select v from meta where k = 'transport'")
        n = 0
        if count(rows) > 0 then
            n = rows[0].v
        end if
        sqlite.close(db)
        return n
    end function

    ' --- offline seam: URL -> fixture filename (matches tools/edgar_capture.sh) --
    ' The endpoint kind is read from the URL's path segments (contains() is an
    ' array test), and the fixture name mirrors the capture script's prefixing.
    function _url_to_fixture(url)
        parts = split(url, "/")
        last = parts[count(parts) - 1]
        if contains(parts, "submissions") then
            return "submissions_" + last
        end if
        if contains(parts, "companyfacts") then
            return "companyfacts_" + last
        end if
        if contains(parts, "Archives") then
            ' filing document: .../data/{cik}/{accession_nodash}/{filename}
            return "doc_" + parts[count(parts) - 2] + "_" + last
        end if
        return "company_tickers.json"
    end function

    ' --- transport (real fetch: fixture read offline, else network) ----------
    function _transport(e, url)
        ' Identity is required for ANY fetch (edgar_design.md §4.1). Checked
        ' before the offline/online split so the rule holds — and is testable —
        ' without a network.
        if is_unknown(e.ident) then
            error "edgar: identity not set — call edgar.identify(e, contact) before fetching"
        end if
        if not is_unknown(e.offline_dir) then
            fname = _url_to_fixture(url)
            path = e.offline_dir + "/" + fname
            pref{file}= path
            if not exists(pref) then
                error "edgar: offline fixture missing for " + url + " (expected " + path + ")"
            end if
            return join(read_lines(pref), "\n")
        end if
        ' online: polite spacing (>= 100ms), declared User-Agent
        sleep(0.1)
        headers = {}
        headers["User-Agent"] = e.ident
        resp = webclient.request({ method: "GET", url: url, headers: headers, timeout: 30 })
        if resp.status != 200 then
            error "edgar: fetch failed (" + string(resp.status) + ") for " + url
        end if
        return resp.body
    end function

    ' --- the single fetch gate: cache-first, then transport -------------------
    function _fetch(e, url, immutable)
        db = _open_cache(e)
        rows = sqlite.query(db, "select body, fetched_at, immutable from cache where url = ?", [url])
        if count(rows) > 0 then
            row = rows[0]
            fresh = false
            if row.immutable = 1 then
                fresh = true
            else
                if epoch() - row.fetched_at < _ttl_seconds() then
                    fresh = true
                end if
            end if
            if fresh then
                body = row.body
                sqlite.close(db)
                return body
            end if
        end if
        ' cache miss (or stale): perform transport, count it, store it.
        body = _transport(e, url)
        _bump_transport(db)
        sqlite.exec(db, "insert or replace into cache (url, body, fetched_at, immutable) values (?, ?, ?, ?)", [url, body, epoch(), immutable])
        sqlite.close(db)
        return body
    end function

    ' --- CIK padding: 10-digit zero-padded string (identifiers are strings) ---
    function _pad_cik(n)
        s = string(n)
        while len(s) < 10
            s = "0" + s
        end while
        return s
    end function

    ' --- public: ticker -> CIK ----------------------------------------------
    function cik(e, ticker)
        body = _fetch(e, _tickers_url(), 0)
        data = decode(body)
        want = upper(ticker)
        for each row in values(data)
            if upper(row.ticker) = want then
                return _pad_cik(row.cik_str)
            end if
        end for
        return unknown
    end function

    ' --- public: submissions -> filing-index frame ---------------------------
    ' A frame is a record whose fields are equal-length column lists; the
    ' submissions "recent" block already stores parallel arrays, so this is a
    ' direct projection. Columns follow the filing-record shape (§3):
    ' form, filed, accession, period, plus primary_document and 8-K items.
    function submissions(e, cik)
        url = _submissions_base() + "/CIK" + cik + ".json"
        body = _fetch(e, url, 0)
        data = decode(body)
        r = data.filings.recent
        return {
            form: r.form,
            filed: r.filingDate,
            accession: r.accessionNumber,
            period: r.reportDate,
            primary_document: r.primaryDocument,
            items: r.items
        }
    end function

    ' --- public: company facts -> decoded XBRL record ------------------------
    ' The whole companyfacts document as a record ({cik, entityName, facts}).
    ' fundamentals.bas walks facts.* ; edgar just decodes and caches it.
    function company_facts(e, cik)
        url = _companyfacts_base() + "/CIK" + cik + ".json"
        body = _fetch(e, url, 0)
        return decode(body)
    end function

    ' --- public: a single filing document ------------------------------------
    ' Filed documents are immutable, so they cache forever (immutable = 1).
    ' URL: .../Archives/edgar/data/{cik-no-leading-zeros}/{accession-no-dashes}/{filename}
    function document(e, cik, accession, filename)
        cikn = string(number(cik))
        nodash = replace(accession, "-", "")
        url = "https://www.sec.gov/Archives/edgar/data/" + cikn + "/" + nodash + "/" + filename
        return _fetch(e, url, 1)
    end function

    ' --- public: poll — new filings since last_seen --------------------------
    ' PURE function over a submissions frame (edgar_design.md §4.1) — no fetch,
    ' so it is fully offline-testable and safe to drive from the monitor loop.
    ' `subs` is a submissions frame (newest-first, as EDGAR returns it).
    ' `last_seen` is the accession of the newest filing already processed; poll
    ' returns the filings strictly newer than it (those ahead of it in the
    ' newest-first order). If last_seen is `unknown` or absent from the frame,
    ' every filing is new.
    function poll(subs, last_seen)
        accs = subs.accession
        cut = count(accs)
        i = 0
        while i < count(accs)
            if accs[i] = last_seen then
                cut = i
                i = count(accs)
            else
                i = i + 1
            end if
        end while
        return _take_cols(subs, cut)
    end function

    ' First n rows of a frame (record of equal-length column lists).
    function _take_cols(df, n)
        out = {}
        for each k in keys(df)
            col = df[k]
            newcol = []
            j = 0
            while j < n
                append(newcol, col[j])
                j = j + 1
            end while
            out[k] = newcol
        end for
        return out
    end function
end library
