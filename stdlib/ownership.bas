' ownership.bas — 13F holdings + quarter-over-quarter delta over the xml module
' (edgar_design.md §1 13F, §4.4, WP-OWN-3).
'
' A 13F information table is a namespaced <informationTable> of <infoTable>
' holdings (issuer, cusip, value, shares). The signal is the QUARTER-OVER-QUARTER
' diff per filer; the data is up to ~4.5 months stale by construction (45-day
' filing deadline) — documented loudly here so callers never mistake it for live
' positioning.
'
' report_13f streams the table via the xml reader's skip_to/subtree windowing
' (constant memory, WP-XML-5) into a frame; delta joins two quarters on cusip.
' Identifiers (cusip) stay strings; value is normalized to whole dollars.
'
' UNIT NORMALIZATION: the SEC's Form 13F amendment made <value> report WHOLE
' DOLLARS for filings on/after 2023-01-03; earlier filings report THOUSANDS of
' dollars. report_13f normalizes by filing date so downstream code never sees the
' seam.
'
' NOTE ON THE PUBLIC SHAPE: the design sketches report_13f(filer_cik, quarter) as
' a network call. The offline-verifiable core shipped here is
' report_13f(source, filed) — `source` is the info-table document (path or file
' ref) and `filed` its filing date. The acquisition wrapper (cik + quarter ->
' locate the quarter's 13F-HR info-table document -> this) is deferred: a 13F
' filing's info-table filename varies per filer and needs the filing index, which
' edgar.bas does not yet expose. Kept honest rather than guessed.
library ownership
    load xml

    ' 13F <value> units by filing date: whole dollars on/after 2023-01-03, else
    ' thousands. ISO date strings compare lexicographically.
    function _normalize_value(raw, filed)
        if filed < "2023-01-03" then
            return raw * 1000
        end if
        return raw
    end function

    ' First index of x in arr, or -1.
    function _index_of(arr, x)
        i = 0
        while i < count(arr)
            if arr[i] = x then
                return i
            end if
            i = i + 1
        end while
        return -1
    end function

    ' --- public: parse a 13F information table into a holdings frame ----------

    ' Stream `source` (a 13F info-table path or file ref) via skip_to/subtree and
    ' return a holdings frame {issuer, cusip, value, shares}. `value` is normalized
    ' to whole dollars by `filed` (see _normalize_value). One row per <infoTable>.
    function report_13f(source, filed)
        r = xml.reader(source)
        issuers = []
        cusips = []
        values = []
        shares = []
        while xml.skip_to(r, "infoTable")
            t = xml.subtree(r)
            append(issuers, xml.text(xml.find(t, "nameOfIssuer")))
            append(cusips, xml.text(xml.find(t, "cusip")))
            append(values, _normalize_value(number(xml.text(xml.find(t, "value"))), filed))
            append(shares, number(xml.text(xml.find(t, "shrsOrPrnAmt/sshPrnamt"))))
        end while
        xml.close(r)
        res = {}
        res["issuer"] = issuers
        res["cusip"] = cusips
        res["value"] = values
        res["shares"] = shares
        return res
    end function

    ' --- public: quarter-over-quarter delta ----------------------------------

    ' Diff two holdings frames on cusip. Returns a frame with one row per cusip in
    ' the UNION of the two quarters and columns: cusip, issuer, status
    ' (new|exited|changed|unchanged), prior_shares, shares, delta_shares,
    ' prior_value, value, delta_value. A `new` position has prior_* = 0 (delta is
    ' the full current); an `exited` position has current-side = 0 (delta negative).
    '
    ' (frame.join is an INNER join, so it yields only the in-both rows — new/exited
    ' are the non-matching rows; delta therefore composes the union explicitly by
    ' cusip rather than through frame.join.)
    function delta(prior, current)
        pc = prior["cusip"]
        cc = current["cusip"]
        cusip_col = []
        issuer_col = []
        status_col = []
        prior_sh = []
        cur_sh = []
        dsh = []
        prior_val = []
        cur_val = []
        dval = []

        ' rows present in the current quarter: new, changed, or unchanged
        i = 0
        while i < count(cc)
            cu = cc[i]
            pidx = _index_of(pc, cu)
            append(cusip_col, cu)
            append(issuer_col, current["issuer"][i])
            append(cur_sh, current["shares"][i])
            append(cur_val, current["value"][i])
            if pidx < 0 then
                append(status_col, "new")
                append(prior_sh, 0)
                append(prior_val, 0)
                append(dsh, current["shares"][i])
                append(dval, current["value"][i])
            else
                ps = prior["shares"][pidx]
                pv = prior["value"][pidx]
                append(prior_sh, ps)
                append(prior_val, pv)
                append(dsh, current["shares"][i] - ps)
                append(dval, current["value"][i] - pv)
                if current["shares"][i] = ps then
                    append(status_col, "unchanged")
                else
                    append(status_col, "changed")
                end if
            end if
            i = i + 1
        end while

        ' rows only in the prior quarter: exited
        i = 0
        while i < count(pc)
            cu = pc[i]
            if _index_of(cc, cu) < 0 then
                append(cusip_col, cu)
                append(issuer_col, prior["issuer"][i])
                append(status_col, "exited")
                append(prior_sh, prior["shares"][i])
                append(prior_val, prior["value"][i])
                append(cur_sh, 0)
                append(cur_val, 0)
                append(dsh, 0 - prior["shares"][i])
                append(dval, 0 - prior["value"][i])
            end if
            i = i + 1
        end while

        res = {}
        res["cusip"] = cusip_col
        res["issuer"] = issuer_col
        res["status"] = status_col
        res["prior_shares"] = prior_sh
        res["shares"] = cur_sh
        res["delta_shares"] = dsh
        res["prior_value"] = prior_val
        res["value"] = cur_val
        res["delta_value"] = dval
        return res
    end function

    ' --- public: 13D/13G stakes (the fast ownership signal) -------------------
    '
    ' Crossing 5% beneficial ownership forces a Schedule 13D (intent to
    ' influence — activism) or 13G (passive). The signal is the CHOICE and any
    ' later flip: the same holder first appearing as 13G and then as 13D just
    ' turned hostile. Days-scale deadlines, so this is the FAST ownership signal
    ' complementing 13F's slow quarterly one.
    '
    ' SCOPE: the SEC mandated STRUCTURED XML (primary_doc.xml, an <edgarSubmission>
    ' with headerData/submissionType) for 13D/G in late 2024. Pre-2025 filings are
    ' free-form text and are OUT OF SCOPE in v1 — stake() raises on a non-structured
    ' document rather than silently returning empty (edgar_design.md §6).
    '
    ' SCHEMA NOTE: 13D and 13G are two schemas that diverge on several tags, so the
    ' field readers below try both spellings (all matching is local-name, so the
    ' schedule13g vs schedule13D namespaces don't matter):
    '   issuer CIK   : issuerCik (13G)            | issuerCIK (13D)
    '   percent      : classPercent (13G)         | percentOfClass (13D)
    '   filer block  : coverPageHeaderReportingPersonDetails (13G)
    '                                             | reportingPersons/reportingPersonInfo (13D)
    '   event date   : eventDateRequiresFilingThisStatement (13G) | dateOfEvent (13D)
    '   cusip        : issuerCusip (13G)          | issuerCusips/issuerCusipNumber (both seen)
    ' The FIRST reporting person is taken as the representative filer/percent; a
    ' multi-person group's other members are not surfaced in v1 (open question,
    ' mirroring 13F per-CIK identity).
    '
    ' NOTE ON THE PUBLIC SHAPE: the design sketches stakes(cik) as a network call
    ' that finds every 13D/G naming `cik` as the subject. The offline-verifiable
    ' core shipped here is stake(source, filed) — parse ONE structured filing into
    ' an event record — plus stakes(rows), which stacks a list of those records into
    ' the events frame. The acquisition wrapper (cik -> locate each 13D/G doc ->
    ' stake each) is deferred: it needs EDGAR full-text enumeration edgar.bas does
    ' not yet expose. Kept honest rather than guessed.

    ' Text at the first of several candidate slash-paths that yields non-empty
    ' text, else "". (xml.text never raises and returns "" for a missing node.)
    function _text_any(node, paths)
        i = 0
        while i < count(paths)
            s = xml.text(xml.find(node, paths[i]))
            if s != "" then
                return s
            end if
            i = i + 1
        end while
        return ""
    end function

    ' Parse ONE structured 13D/13G primary_doc.xml into an event record:
    '   filer, form ("13D"|"13G"), percent (number or "unknown"), filed,
    '   amended (true|false), issuer_cik, issuer_name, cusip, event_date.
    ' `source` is the document (path or file ref); `filed` its filing date
    ' (ISO string — not carried in the XML, so supplied by the caller from the
    ' filing metadata, exactly as report_13f takes `filed`). Identifiers
    ' (issuer_cik, cusip) stay strings. Raises on a non-structured filing.
    function stake(source, filed)
        root = xml.parse_file(source)
        st = _text_any(root, ["headerData/submissionType"])
        if st = "" then
            error "ownership.stake: not a structured 13D/13G filing (no headerData/submissionType; pre-2025 text filings are out of scope)"
        end if
        is13d = find(st, "13D") != nothing
        is13g = find(st, "13G") != nothing
        if not is13d and not is13g then
            error "ownership.stake: submissionType '" + st + "' is not a Schedule 13D or 13G"
        end if

        row = {}
        if is13d then
            row["form"] = "13D"
        else
            row["form"] = "13G"
        end if
        row["amended"] = find(st, "/A") != nothing
        row["filer"] = _text_any(root, [
            "formData/coverPageHeaderReportingPersonDetails/reportingPersonName",
            "formData/reportingPersons/reportingPersonInfo/reportingPersonName"
        ])
        pstr = _text_any(root, [
            "formData/coverPageHeaderReportingPersonDetails/classPercent",
            "formData/reportingPersons/reportingPersonInfo/percentOfClass"
        ])
        if pstr = "" then
            row["percent"] = "unknown"
        else
            row["percent"] = number(pstr)
        end if
        row["issuer_name"] = _text_any(root, ["formData/coverPageHeader/issuerInfo/issuerName"])
        row["issuer_cik"] = _text_any(root, [
            "formData/coverPageHeader/issuerInfo/issuerCik",
            "formData/coverPageHeader/issuerInfo/issuerCIK"
        ])
        row["cusip"] = _text_any(root, [
            "formData/coverPageHeader/issuerInfo/issuerCusip",
            "formData/coverPageHeader/issuerInfo/issuerCusips/issuerCusipNumber"
        ])
        row["event_date"] = _text_any(root, [
            "formData/coverPageHeader/eventDateRequiresFilingThisStatement",
            "formData/coverPageHeader/dateOfEvent"
        ])
        row["filed"] = filed
        return row
    end function

    ' Stack a list of stake() records (chronological events for one subject) into
    ' an events frame with the columns stake() emits: filer, form, percent, filed,
    ' amended, issuer_cik, issuer_name, cusip, event_date. A holder that files 13G
    ' then 13D shows as two rows with the form column flipping 13G -> 13D — the
    ' passive->activist signal. (Built manually, matching report_13f/delta, so
    ' ownership.bas depends only on the xml module.)
    function stakes(rows)
        cols = ["filer", "form", "percent", "filed", "amended", "issuer_cik", "issuer_name", "cusip", "event_date"]
        res = {}
        c = 0
        while c < count(cols)
            res[cols[c]] = []
            c = c + 1
        end while
        i = 0
        while i < count(rows)
            c = 0
            while c < count(cols)
                append(res[cols[c]], rows[i][cols[c]])
                c = c + 1
            end while
            i = i + 1
        end while
        return res
    end function
end library
