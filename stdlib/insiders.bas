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

' insiders.bas — Form 4 insider transactions over the xml module (edgar_design.md
' §1 Form 4, §4.3, WP-OWN-1).
'
' A Form 4 is clean XML: an <ownershipDocument> with one <reportingOwner> and a
' <nonDerivativeTable> of <nonDerivativeTransaction> rows (open-market buys/sells,
' award settlements, tax withholding). Each transaction carries a one-letter
' code; code P — open-market purchase — is the only voluntary buy with the
' insider's own money (§1), so it is the signal `open_market_buys` isolates.
'
' Output is a FRAME (record of equal-length column lists, edgar_design.md §3) with
' the §4.3 columns: code, acquired (A/D), shares, price, value, owner, is_officer,
' is_director, officer_title, post_shares, filed, date. Screening is frame.filter;
' multiple Form 4s per filer concatenate (frame concat). The library ranks and
' recommends nothing — it produces the columns humans and the judgment layer
' reason over.
'
' Scope note (v1): the NON-DERIVATIVE table only. Derivative transactions (option
' grants/exercises) are a separate analytical surface and are deferred; the §4.3
' vocabulary (A/D, price, code-P open-market buy) is the non-derivative table.
'
' Identifiers stay strings; dollar amounts are numbers; a missing/footnoted cell
' (e.g. an RSU-settlement price shown only as a footnote) is `unknown`, never a
' guess (the standing NA policy).
library insiders
    load xml
    load frame from "frame.bas"
    load edgar from "edgar.bas"
    load dates from "dates.bas"

    ' --- private helpers -----------------------------------------------------

    ' Text at a slash-path under a node, or "" when absent (xml.text never raises).
    function _txt(node, path)
        return xml.text(xml.find(node, path))
    end function

    ' A numeric cell: a value string -> number; an empty (absent/footnoted) cell
    ' -> unknown. Never calls number("") (which raises).
    function _num(s)
        if s = "" then
            return unknown
        end if
        return number(s)
    end function

    ' A boolean relationship flag ("true"/anything-else), absent -> false.
    function _flag(node, path)
        if xml.text(xml.find(node, path)) = "true" then
            return true
        end if
        return false
    end function

    ' Build one §4.3 row from a <nonDerivativeTransaction> element. Owner and
    ' relationship fields are filing-level (shared by every transaction in the
    ' document), so they are passed in. `filed` is the SEC acceptance date from
    ' submissions — NOT in the Form 4 XML (which carries only the signature date),
    ' so the caller supplies it.
    function _row(txn, owner, is_officer, is_director, officer_title, filed)
        shares = _num(_txt(txn, "transactionAmounts/transactionShares/value"))
        price = _num(_txt(txn, "transactionAmounts/transactionPricePerShare/value"))
        value = unknown
        if not is_unknown(shares) then
            if not is_unknown(price) then
                value = shares * price
            end if
        end if
        return {
            code: _txt(txn, "transactionCoding/transactionCode"),
            acquired: _txt(txn, "transactionAmounts/transactionAcquiredDisposedCode/value"),
            shares: shares,
            price: price,
            value: value,
            owner: owner,
            is_officer: is_officer,
            is_director: is_director,
            officer_title: officer_title,
            post_shares: _num(_txt(txn, "postTransactionAmounts/sharesOwnedFollowingTransaction/value")),
            filed: filed,
            date: _txt(txn, "transactionDate/value")
        }
    end function

    function _is_code_p(row)
        return row["code"] = "P"
    end function

    ' Last path segment of a primaryDocument (Form 4 primaryDocument is often an
    ' `xslF345X0N/<name>.xml` rendered path; the raw XML is the trailing name).
    function _raw_doc_name(name)
        parts = split(name, "/")
        return parts[len(parts) - 1]
    end function

    ' --- public: parse one Form 4 document -----------------------------------

    ' Parse a Form 4 document (a parsed-xml record from xml.parse) into a
    ' transaction frame — one row per non-derivative transaction, all §4.3
    ' columns. `filed` is the filing date from submissions (see _row).
    function from_form4(doc, filed)
        owner = _txt(doc, "reportingOwner/reportingOwnerId/rptOwnerName")
        is_officer = _flag(doc, "reportingOwner/reportingOwnerRelationship/isOfficer")
        is_director = _flag(doc, "reportingOwner/reportingOwnerRelationship/isDirector")
        officer_title = _txt(doc, "reportingOwner/reportingOwnerRelationship/officerTitle")
        txns = xml.find_all(doc, "nonDerivativeTable/nonDerivativeTransaction")
        rows = []
        i = 0
        while i < len(txns)
            append(rows, _row(txns[i], owner, is_officer, is_director, officer_title, filed))
            i = i + 1
        end while
        return frame.from_rows(rows)
    end function

    ' --- public: concat multiple Form 4s -------------------------------------

    ' Concatenate two transaction frames (multiple Form 4s per filer). Row-wise
    ' append; the frames always share the _row column shape. An empty operand
    ' (`{}` from a transaction-less Form 4) is handled — its row list is empty.
    function concat(a, b)
        rows = frame.to_rows(a)
        more = frame.to_rows(b)
        i = 0
        while i < len(more)
            append(rows, more[i])
            i = i + 1
        end while
        return frame.from_rows(rows)
    end function

    ' --- public: the code-P screen -------------------------------------------

    ' Open-market purchases only: transaction code P — the one voluntary buy with
    ' the insider's own money (§1). A (award), M (option exercise), F (tax
    ' withholding), S (sale) and the rest are filtered out.
    function open_market_buys(tx)
        return frame.filter(tx, _is_code_p)
    end function

    ' --- public: acquisition-backed convenience ------------------------------

    ' Fetch every Form 4 for `cik` filed on/after `since` (a YYYY-MM-DD string)
    ' and return the combined transaction frame. `e` is an edgar session handle
    ' (edgar_design.md §4.1). This is the network entry point; the offline golden
    ' drives from_form4/concat/open_market_buys directly over the checked-in
    ' fixture, so this composition is documented-but-not-exercised in tests.
    function transactions(e, cik, since)
        subs = edgar.submissions(e, cik)
        forms = subs.form
        out = {}
        i = 0
        while i < count(forms)
            if forms[i] = "4" then
                if subs.filed[i] >= since then
                    fname = _raw_doc_name(subs.primary_document[i])
                    body = edgar.document(e, cik, subs.accession[i], fname)
                    doc = xml.parse(body)
                    out = concat(out, from_form4(doc, subs.filed[i]))
                end if
            end if
            i = i + 1
        end while
        return out
    end function

    ' --- public: conviction (edgar_design.md §4.3) ---------------------------

    ' Buy size vs prior stake. Prior stake (the holding BEFORE the buy) is
    ' post_shares - shares; conviction is value / prior_stake (WP-OWN-2). Adds two
    ' columns (prior_stake, conviction) to the buys frame. A zero prior stake — a
    ' first-ever purchase, where post_shares == shares — or any unknown ingredient
    ' yields `unknown`, never a divide-by-zero.
    function conviction(buys)
        rows = frame.to_rows(buys)
        out = []
        i = 0
        while i < len(rows)
            r = rows[i]
            prior = unknown
            if not is_unknown(r["post_shares"]) then
                if not is_unknown(r["shares"]) then
                    prior = r["post_shares"] - r["shares"]
                end if
            end if
            conv = unknown
            if not is_unknown(prior) then
                if prior != 0 then
                    if not is_unknown(r["value"]) then
                        conv = r["value"] / prior
                    end if
                end if
            end if
            r["prior_stake"] = prior
            r["conviction"] = conv
            append(out, r)
            i = i + 1
        end while
        return frame.from_rows(out)
    end function

    ' --- public: cluster (edgar_design.md §4.3) ------------------------------

    ' Group buys into clusters by date proximity — each buy within `window` days
    ' of its cluster's FIRST buy — and summarize each: distinct owners, buy count,
    ' total value, date span (start..end). `owners` >= 2 is the multi-insider
    ' cluster signal (several insiders buying near-simultaneously). Input is sorted
    ' by date internally (ISO dates sort chronologically). A cluster's value is
    ' `unknown` if any member buy's value is unknown.
    function cluster(buys, window)
        sorted = frame.sort_by(buys, "date")
        rows = frame.to_rows(sorted)
        starts = []
        spans_end = []
        owners_col = []
        buys_col = []
        value_col = []
        n = len(rows)
        i = 0
        while i < n
            cstart = rows[i]["date"]
            cend = rows[i]["date"]
            names = []
            append(names, rows[i]["owner"])
            vsum = rows[i]["value"]
            cnt = 1
            j = i + 1
            while j < n
                if dates.days_between(cstart, rows[j]["date"]) <= window then
                    cend = rows[j]["date"]
                    append(names, rows[j]["owner"])
                    if is_unknown(vsum) then
                        vsum = unknown
                    else
                        if is_unknown(rows[j]["value"]) then
                            vsum = unknown
                        else
                            vsum = vsum + rows[j]["value"]
                        end if
                    end if
                    cnt = cnt + 1
                    j = j + 1
                else
                    j = n
                end if
            end while
            append(starts, cstart)
            append(spans_end, cend)
            append(owners_col, count(unique(names)))
            append(buys_col, cnt)
            append(value_col, vsum)
            i = i + cnt
        end while
        res = {}
        res["start"] = starts
        res["end"] = spans_end
        res["owners"] = owners_col
        res["buys"] = buys_col
        res["value"] = value_col
        return res
    end function
end library
