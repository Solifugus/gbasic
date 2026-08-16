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

' screener.bas — the whole-market bulk tier (edgar_design.md §4.6, §8.5, WP-SCR-1).
'
' The nightly companyfacts.zip holds one CIK{10}.json per filer. Ingest once into
' a local sqlite universe, and screening the market becomes local frame work.
'
' ZIP DECISION (edgar_design.md §8.5, recorded here): gBASIC has no DEFLATE and no
' binary-safe unzip, so `ingest` does NOT read the .zip directly. It consumes an
' ALREADY-EXTRACTED DIRECTORY of CIK{10}.json files — the user runs
'   unzip companyfacts.zip -d some_dir
' and passes `some_dir`. This is the design's honest fallback that keeps the core
' untouched (no new C). tools/screener_sample_build.sh produces the equivalent
' extracted directory for the test fixtures.
'
' RESUMABILITY: ingestion is a batch job (the real archive is gigabytes / hours).
' The universe table is keyed by cik; ingest SKIPS filers already present, so a
' re-run continues where it left off and never duplicates. ingest_limited(cap)
' processes at most `cap` new filers per call — the hook the test uses to simulate
' an interruption.
library screener
    load sqlite
    load fundamentals from "fundamentals.bas"
    load forensics from "forensics.bas"

    ' --- schema -------------------------------------------------------------
    function _open(db_path)
        db = sqlite.connect(db_path)
        sqlite.exec(db, "create table if not exists universe (cik text primary key, name text, latest_period text, fact_count integer, ingested_at integer)")
        return db
    end function

    ' text or "" (identifiers/text columns never store a guessed value; a missing
    ' field becomes empty, not invented).
    function _txt(v)
        if is_string(v) then
            return v
        end if
        return ""
    end function

    ' "CIK0000320193.json" -> "0000320193"; "" for anything that isn't a CIK facts
    ' file (e.g. a stray .gitkeep). CIK stays a 10-digit zero-padded STRING.
    function _cik_from_filename(f)
        nm = file_name(f)
        if find(nm, "CIK") != 0 then
            return ""
        end if
        rest = mid(nm, 3, len(nm) - 3)
        dot = find(rest, ".json")
        if dot = nothing then
            return ""
        end if
        return left(rest, dot)
    end function

    ' Latest period = max XBRL fact `end` date across us-gaap facts, or `unknown`.
    ' Walks with for-each (indexing arr[i] in a while-loop is O(n^2) in gBASIC).
    function _latest_period(data)
        if not has(data, "facts") then
            return unknown
        end if
        facts = data["facts"]
        if not has(facts, "us-gaap") then
            return unknown
        end if
        best = ""
        for each concept in values(facts["us-gaap"])
            if has(concept, "units") then
                for each entries in values(concept["units"])
                    for each e in entries
                        en = e["end"]
                        if is_string(en) and en > best then
                            best = en
                        end if
                    end for
                end for
            end if
        end for
        if best = "" then
            return unknown
        end if
        return best
    end function

    ' Summarize a decoded companyfacts record for the universe row.
    function _summarize(data)
        fc = 0
        if has(data, "facts") and has(data["facts"], "us-gaap") then
            fc = count(keys(data["facts"]["us-gaap"]))
        end if
        out = {}
        out["name"] = data["entityName"]
        out["latest_period"] = _latest_period(data)
        out["fact_count"] = fc
        return out
    end function

    ' --- public: bulk ingest -------------------------------------------------

    ' Ingest at most `cap` NOT-YET-INGESTED filers from `facts_dir` (a directory of
    ' CIK{10}.json files) into the sqlite universe at `db_path`. cap <= 0 means no
    ' limit. Returns the number of filers ingested THIS call. Resumable: filers
    ' already in the universe are skipped, so re-running never duplicates.
    function ingest_limited(db_path, facts_dir, cap)
        db = _open(db_path)
        processed = 0
        for each f in list_files(facts_dir)
            cik = _cik_from_filename(f)
            if cik != "" then
                existing = sqlite.query(db, "select 1 as present from universe where cik = ?", [cik])
                if count(existing) = 0 then
                    data = decode(join(read_lines(f), "\n"))
                    s = _summarize(data)
                    sqlite.exec(db, "insert or replace into universe (cik, name, latest_period, fact_count, ingested_at) values (?, ?, ?, ?, ?)", [cik, _txt(s["name"]), _txt(s["latest_period"]), s["fact_count"], epoch()])
                    processed = processed + 1
                    if cap > 0 and processed >= cap then
                        break
                    end if
                end if
            end if
        end for
        sqlite.close(db)
        return processed
    end function

    ' Full ingest (all filers in the directory), resumable.
    function ingest(db_path, facts_dir)
        return ingest_limited(db_path, facts_dir, 0)
    end function

    ' --- public: the universe frame -----------------------------------------

    ' { cik, name, latest_period } — one row per ingested filer, ordered by cik.
    function universe(db_path)
        db = sqlite.connect(db_path)
        rows = sqlite.query(db, "select cik, name, latest_period from universe order by cik")
        sqlite.close(db)
        ciks = []
        names = []
        periods = []
        for each r in rows
            append(ciks, r.cik)
            append(names, r.name)
            append(periods, r.latest_period)
        end for
        out = {}
        out["cik"] = ciks
        out["name"] = names
        out["latest_period"] = periods
        return out
    end function

    ' ========================================================================
    ' Cross-sectional scoring (edgar_design.md §4.6, WP-SCR-2). §4.5's forensic
    ' scores computed per filer and CACHED in sqlite, INCREMENTALLY: a filer
    ' already in `scores` is skipped, so a re-run resumes and never recomputes.
    ' Each score is the LATEST fiscal year's headline; a missing ingredient stays
    ' `unknown` all the way through the cache (stored as SQL NULL, since `unknown`
    ' is not a bindable parameter, and restored to `unknown` on read).

    function _open_scores(db_path)
        db = sqlite.connect(db_path)
        sqlite.exec(db, "create table if not exists scores (cik text primary key, name text, latest_period text, piotroski, mscore, mscore_flag, accrual_ratio, fcf, unknown_concepts text, scored_at integer)")
        return db
    end function

    ' unknown -> nothing (binds as SQL NULL); everything else (number, boolean)
    ' passes through. booleans bind as 1/0.
    function _bind(v)
        if is_unknown(v) then
            return nothing
        end if
        return v
    end function

    ' SQL NULL comes back as `nothing`; restore it to `unknown` (the NA policy).
    function _unwrap(v)
        if is_nothing(v) then
            return unknown
        end if
        return v
    end function

    ' mscore_flag round-trips through 1/0/NULL -> true/false/unknown.
    function _unflag(v)
        if is_nothing(v) then
            return unknown
        end if
        if v = 1 then
            return true
        end if
        return false
    end function

    ' latest value of a column in an FY-only forensic frame (rows are end-ascending
    ' and one per fiscal year, so the last row is the latest year), or `unknown`.
    function _last(frame, col)
        c = frame[col]
        n = count(c)
        if n = 0 then
            return unknown
        end if
        return c[n - 1]
    end function

    ' latest FY value of a column in a fundamentals frame (which interleaves FY and
    ' quarterly rows), or `unknown`.
    function _fy_last(frame, col)
        v = unknown
        k = 0
        while k < count(frame["fp"])
            if frame["fp"][k] = "FY" then
                v = frame[col][k]
            end if
            k = k + 1
        end while
        return v
    end function

    ' comma-joined list of concept keys that are UNKNOWN for this filer — the raw
    ' material of the concept-map report card. "" when every concept is present.
    function _unknown_concepts(data)
        miss = []
        for each c in fundamentals.concepts()
            s = fundamentals.series(data, c)
            if is_unknown(s) then
                append(miss, c)
            end if
        end for
        return join(miss, ",")
    end function

    ' the latest-FY headline scores for one decoded companyfacts record.
    function _score_row(data)
        p = forensics.piotroski(data)
        b = forensics.beneish(data)
        ac = forensics.accruals(data)
        fc = fundamentals.fcf(data)
        out = {}
        out["piotroski"] = _last(p, "f_score")
        out["mscore"] = _last(b, "mscore")
        out["mscore_flag"] = _last(b, "flag")
        out["accrual_ratio"] = _last(ac, "accrual_ratio")
        out["fcf"] = _fy_last(fc, "value")
        out["unknown_concepts"] = _unknown_concepts(data)
        return out
    end function

    ' Score at most `cap` NOT-YET-SCORED filers from `facts_dir` into `db_path`
    ' (cap <= 0 = no limit). Returns the number scored THIS call. Resumable.
    function score_limited(db_path, facts_dir, cap)
        db = _open_scores(db_path)
        processed = 0
        for each f in list_files(facts_dir)
            cik = _cik_from_filename(f)
            if cik != "" then
                existing = sqlite.query(db, "select 1 as present from scores where cik = ?", [cik])
                if count(existing) = 0 then
                    data = decode(join(read_lines(f), "\n"))
                    s = _score_row(data)
                    sqlite.exec(db, "insert or replace into scores (cik, name, latest_period, piotroski, mscore, mscore_flag, accrual_ratio, fcf, unknown_concepts, scored_at) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", [cik, _txt(data["entityName"]), _txt(_latest_period(data)), _bind(s["piotroski"]), _bind(s["mscore"]), _bind(s["mscore_flag"]), _bind(s["accrual_ratio"]), _bind(s["fcf"]), s["unknown_concepts"], epoch()])
                    processed = processed + 1
                    if cap > 0 and processed >= cap then
                        break
                    end if
                end if
            end if
        end for
        sqlite.close(db)
        return processed
    end function

    ' Full scoring pass (all filers), resumable.
    function score(db_path, facts_dir)
        return score_limited(db_path, facts_dir, 0)
    end function

    ' --- public: the scored universe frame ----------------------------------
    ' { cik, name, latest_period, piotroski, mscore, mscore_flag, accrual_ratio,
    ' fcf } — one row per scored filer, cik-ordered. NULLs restored to `unknown`.
    function scored(db_path)
        db = sqlite.connect(db_path)
        rows = sqlite.query(db, "select cik, name, latest_period, piotroski, mscore, mscore_flag, accrual_ratio, fcf from scores order by cik")
        sqlite.close(db)
        out = {}
        out["cik"] = []
        out["name"] = []
        out["latest_period"] = []
        out["piotroski"] = []
        out["mscore"] = []
        out["mscore_flag"] = []
        out["accrual_ratio"] = []
        out["fcf"] = []
        for each r in rows
            append(out["cik"], r.cik)
            append(out["name"], r.name)
            append(out["latest_period"], r.latest_period)
            append(out["piotroski"], _unwrap(r.piotroski))
            append(out["mscore"], _unwrap(r.mscore))
            append(out["mscore_flag"], _unflag(r.mscore_flag))
            append(out["accrual_ratio"], _unwrap(r.accrual_ratio))
            append(out["fcf"], _unwrap(r.fcf))
        end for
        return out
    end function

    ' --- public: run — the market screen ------------------------------------
    ' Apply predicate `fn` (a function value taking a per-filer record) to each row
    ' of a frame `u` (the scored universe); keep rows where fn returns exactly
    ' `true` (an `unknown`/`false` verdict excludes — an unscored filer is never a
    ' hit). Returns a frame with the same columns.
    function run(u, fn)
        cols = keys(u)
        out = {}
        for each c in cols
            out[c] = []
        end for
        n = 0
        if count(cols) > 0 then
            n = count(u[cols[0]])
        end if
        i = 0
        while i < n
            row = {}
            for each c in cols
                row[c] = u[c][i]
            end for
            keep = fn(row)
            if keep = true then
                for each c in cols
                    append(out[c], u[c][i])
                end for
            end if
            i = i + 1
        end while
        return out
    end function

    ' --- public: the concept-map report card --------------------------------
    ' Per concept, how many scored filers LACK it — the `unknown` long tail that
    ' tells the truth about coverage (edgar_design.md §4.6). Returns
    ' { filers, concept:[...], unknown_count:[...], rate:[...] } in concept-map
    ' order; every known concept appears (a fully-covered concept as count 0).
    function unknown_report(db_path)
        db = sqlite.connect(db_path)
        rows = sqlite.query(db, "select unknown_concepts from scores")
        sqlite.close(db)
        filers = count(rows)
        tally = {}
        for each c in fundamentals.concepts()
            tally[c] = 0
        end for
        for each r in rows
            uc = r.unknown_concepts
            if is_string(uc) and uc != "" then
                for each c in split(uc, ",")
                    if has(tally, c) then
                        tally[c] = tally[c] + 1
                    end if
                end for
            end if
        end for
        concepts_out = []
        counts_out = []
        rates_out = []
        for each c in fundamentals.concepts()
            append(concepts_out, c)
            append(counts_out, tally[c])
            rate = 0
            if filers > 0 then
                rate = round(tally[c] / filers, 2)
            end if
            append(rates_out, rate)
        end for
        out = {}
        out["filers"] = filers
        out["concept"] = concepts_out
        out["unknown_count"] = counts_out
        out["rate"] = rates_out
        return out
    end function
end library
