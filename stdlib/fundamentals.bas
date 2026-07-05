' fundamentals.bas — 10-K/10-Q numerics over EDGAR companyfacts (edgar_design.md
' §4.2, WP-EDG-4).
'
' The real work is XBRL tag normalization: companies tag the same concept with
' different us-gaap tags, and facts repeat across a 10-K and later 10-Qs. This
' module owns:
'   * the CONCEPT MAP — a curated concept -> ordered tag fallback chain; first
'     present wins, none present => `unknown` (never a guess, per the NA policy).
'   * SERIES with period dedup — collapse repeats by (start, end, fiscal period),
'     keeping the LATEST-FILED value (restatements win); series_as_filed keeps
'     the originals so restatement deltas stay inspectable.
'
' A fact series is a frame (record of equal-length column lists), matching
' edgar_design.md §3: end, start, value, fy, fp, form, accession, filed.
' Fact-object fields are read with bracket access because `end` is a reserved
' word (f["end"], not f.end).
library fundamentals

    ' --- concept map: concept -> ordered us-gaap/dei tag fallback chain -------
    ' Covers the §4.2 v2 list. First tag present in companyfacts wins.
    function _concept_map()
        return {
            revenue: ["RevenueFromContractWithCustomerExcludingAssessedTax", "Revenues", "SalesRevenueNet"],
            cost_of_revenue: ["CostOfGoodsAndServicesSold", "CostOfRevenue", "CostOfGoodsSold"],
            gross_profit: ["GrossProfit"],
            sga: ["SellingGeneralAndAdministrativeExpense", "GeneralAndAdministrativeExpense"],
            dep_amort: ["DepreciationDepletionAndAmortization", "DepreciationAmortizationAndAccretionNet", "DepreciationAndAmortization", "Depreciation"],
            operating_income: ["OperatingIncomeLoss"],
            net_income: ["NetIncomeLoss", "ProfitLoss"],
            interest_expense: ["InterestExpense", "InterestExpenseNonoperating", "InterestAndDebtExpense"],
            operating_cash_flow: ["NetCashProvidedByUsedInOperatingActivities", "NetCashProvidedByUsedInOperatingActivitiesContinuingOperations"],
            capex: ["PaymentsToAcquirePropertyPlantAndEquipment", "PaymentsToAcquireProductiveAssets"],
            cash: ["CashAndCashEquivalentsAtCarryingValue", "CashCashEquivalentsRestrictedCashAndRestrictedCashEquivalents"],
            receivables: ["AccountsReceivableNetCurrent", "ReceivablesNetCurrent"],
            current_assets: ["AssetsCurrent"],
            current_liabilities: ["LiabilitiesCurrent"],
            ppe_net: ["PropertyPlantAndEquipmentNet"],
            total_assets: ["Assets"],
            total_liabilities: ["Liabilities"],
            long_term_debt: ["LongTermDebtNoncurrent", "LongTermDebt"],
            long_term_debt_current: ["LongTermDebtCurrent", "DebtCurrent"],
            retained_earnings: ["RetainedEarningsAccumulatedDeficit"],
            book_equity: ["StockholdersEquity", "StockholdersEquityIncludingPortionAttributableToNoncontrollingInterest"],
            shares_outstanding: ["CommonStockSharesOutstanding", "EntityCommonStockSharesOutstanding", "WeightedAverageNumberOfDilutedSharesOutstanding"],
            share_based_comp: ["ShareBasedCompensation"],
            stock_repurchased: ["PaymentsForRepurchaseOfCommonStock"],
            dividends_paid: ["PaymentsOfDividendsCommon", "PaymentsOfDividends"]
        }
    end function

    ' The us-gaap concept tags this library knows (for reporting/coverage).
    function concepts()
        return keys(_concept_map())
    end function

    ' --- tag resolution -----------------------------------------------------
    ' Return the fact node ({label?, units:{UNIT:[...]}}) for the first tag in
    ' the chain present under us-gaap (then dei), or `unknown` if none present.
    function _find_tag_node(facts, chain)
        allf = facts.facts
        gaap = allf["us-gaap"]
        havedei = has(allf, "dei")
        for each tag in chain
            if has(gaap, tag) then
                return gaap[tag]
            end if
            if havedei then
                if has(allf["dei"], tag) then
                    return allf["dei"][tag]
                end if
            end if
        end for
        return unknown
    end function

    ' Pick the fact array for a node's unit: prefer USD, then shares, else first.
    function _pick_unit(node)
        u = node.units
        if has(u, "USD") then
            return u["USD"]
        end if
        if has(u, "shares") then
            return u["shares"]
        end if
        ks = keys(u)
        return u[ks[0]]
    end function

    ' --- raw rows (list of fact records) ------------------------------------
    ' Returns `unknown` when the concept is unmapped OR no tag in its chain is
    ' present (the NA policy: absence is unknown, never a guess).
    function _raw_rows(facts, concept)
        map = _concept_map()
        if not has(map, concept) then
            return unknown
        end if
        node = _find_tag_node(facts, map[concept])
        if is_unknown(node) then
            return unknown
        end if
        ulist = _pick_unit(node)
        rows = []
        for each f in ulist
            st = ""
            if has(f, "start") then
                st = f["start"]
            end if
            r = {}
            r["end"] = f["end"]
            r["start"] = st
            r["value"] = f["val"]
            r["fy"] = f["fy"]
            r["fp"] = f["fp"]
            r["form"] = f["form"]
            r["accession"] = f["accn"]
            r["filed"] = f["filed"]
            append(rows, r)
        end for
        return rows
    end function

    ' --- dedup: latest-filed per (start, end, fp) ----------------------------
    ' "Latest accession" is resolved as latest `filed` date (accession strings
    ' are not globally ordered — companyfacts mixes filing agents), accession as
    ' the tiebreaker.
    function _later(a, b)
        if a["filed"] != b["filed"] then
            return a["filed"] > b["filed"]
        end if
        return a["accession"] > b["accession"]
    end function

    function _dedup(rows)
        best = {}
        for each r in rows
            k = r["start"] + "|" + r["end"] + "|" + r["fp"]
            if has(best, k) then
                if _later(r, best[k]) then
                    best[k] = r
                end if
            else
                best[k] = r
            end if
        end for
        return values(best)
    end function

    ' --- sort rows ascending by (end, start) --------------------------------
    function _row_key(r)
        return r["end"] + "|" + r["start"]
    end function

    function _sort_rows(rows)
        n = count(rows)
        i = 1
        while i < n
            j = i
            while j > 0
                if _row_key(rows[j]) < _row_key(rows[j - 1]) then
                    tmp = rows[j]
                    rows[j] = rows[j - 1]
                    rows[j - 1] = tmp
                    j = j - 1
                else
                    j = 0
                end if
            end while
            i = i + 1
        end while
        return rows
    end function

    ' --- rows -> fact frame (column-major) ----------------------------------
    function _rows_to_frame(rows)
        ends = []
        starts = []
        vals = []
        fys = []
        fps = []
        forms = []
        accs = []
        fileds = []
        for each r in rows
            append(ends, r["end"])
            append(starts, r["start"])
            append(vals, r["value"])
            append(fys, r["fy"])
            append(fps, r["fp"])
            append(forms, r["form"])
            append(accs, r["accession"])
            append(fileds, r["filed"])
        end for
        out = {}
        out["end"] = ends
        out["start"] = starts
        out["value"] = vals
        out["fy"] = fys
        out["fp"] = fps
        out["form"] = forms
        out["accession"] = accs
        out["filed"] = fileds
        return out
    end function

    ' --- public: series (deduped) -------------------------------------------
    function series(facts, concept)
        rows = _raw_rows(facts, concept)
        if is_unknown(rows) then
            return unknown
        end if
        rows = _dedup(rows)
        rows = _sort_rows(rows)
        return _rows_to_frame(rows)
    end function

    ' --- public: series_as_filed (originals kept, no dedup) ------------------
    function series_as_filed(facts, concept)
        rows = _raw_rows(facts, concept)
        if is_unknown(rows) then
            return unknown
        end if
        rows = _sort_rows(rows)
        return _rows_to_frame(rows)
    end function

    ' ========================================================================
    ' Derived metrics (WP-EDG-5). All compose over series and align by period
    ' key (end, fp) — a fiscal year-end shares that key across duration (flow)
    ' and instant (balance-sheet) facts, so cross-type alignment is exact.
    ' Every output is a fact frame; a missing ingredient propagates as `unknown`
    ' for that period (the standing NA policy), never a guessed value.
    ' ========================================================================

    ' period key for alignment
    function _pk(endv, fp)
        return endv + "|" + fp
    end function

    ' index a frame's column by period key: (end|fp) -> value
    function _index_col(frame, col)
        idx = {}
        if is_unknown(frame) then
            return idx
        end if
        i = 0
        while i < count(frame["end"])
            idx[_pk(frame["end"][i], frame["fp"][i])] = frame[col][i]
            i = i + 1
        end while
        return idx
    end function

    function _get(idx, k)
        if has(idx, k) then
            return idx[k]
        end if
        return unknown
    end function

    ' NA-propagating arithmetic
    function _add(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return a + b
    end function

    function _sub(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return a - b
    end function

    function _div(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        if b = 0 then
            return unknown
        end if
        return a / b
    end function

    ' period skeleton {end, fp, fy} from a driver series (the concept that
    ' defines the periods of interest)
    function _periods(s)
        ps = []
        if is_unknown(s) then
            return ps
        end if
        i = 0
        while i < count(s["end"])
            p = {}
            p["end"] = s["end"][i]
            p["fp"] = s["fp"][i]
            p["fy"] = s["fy"][i]
            append(ps, p)
            i = i + 1
        end while
        return ps
    end function

    ' --- fcf: operating cash flow - capex, per period ------------------------
    function fcf(facts)
        ocf = series(facts, "operating_cash_flow")
        cap = series(facts, "capex")
        ocfi = _index_col(ocf, "value")
        capi = _index_col(cap, "value")
        ends = []
        fps = []
        fys = []
        vals = []
        for each p in _periods(ocf)
            k = _pk(p["end"], p["fp"])
            append(ends, p["end"])
            append(fps, p["fp"])
            append(fys, p["fy"])
            append(vals, _sub(_get(ocfi, k), _get(capi, k)))
        end for
        out = {}
        out["end"] = ends
        out["fp"] = fps
        out["fy"] = fys
        out["value"] = vals
        return out
    end function

    ' --- debt: total / current / noncurrent / net ----------------------------
    ' noncurrent = long-term debt; current = current portion (absent => treated
    ' as 0 for the total, but reported as unknown in its own column); total =
    ' noncurrent + current; net = total - cash & equivalents.
    function debt(facts)
        nc = series(facts, "long_term_debt")
        cur = series(facts, "long_term_debt_current")
        cash = series(facts, "cash")
        nci = _index_col(nc, "value")
        curi = _index_col(cur, "value")
        cashi = _index_col(cash, "value")
        ends = []
        fps = []
        fys = []
        totals = []
        currents = []
        noncurrents = []
        nets = []
        for each p in _periods(nc)
            k = _pk(p["end"], p["fp"])
            ncv = _get(nci, k)
            curv = _get(curi, k)
            curv0 = curv
            if is_unknown(curv0) then
                curv0 = 0
            end if
            total = _add(ncv, curv0)
            net = _sub(total, _get(cashi, k))
            append(ends, p["end"])
            append(fps, p["fp"])
            append(fys, p["fy"])
            append(noncurrents, ncv)
            append(currents, curv)
            append(totals, total)
            append(nets, net)
        end for
        out = {}
        out["end"] = ends
        out["fp"] = fps
        out["fy"] = fys
        out["total"] = totals
        out["current"] = currents
        out["noncurrent"] = noncurrents
        out["net"] = nets
        return out
    end function

    ' --- margins: gross / operating / net (as fractions of revenue) ----------
    function margins(facts)
        rev = series(facts, "revenue")
        gp = series(facts, "gross_profit")
        oi = series(facts, "operating_income")
        ni = series(facts, "net_income")
        revi = _index_col(rev, "value")
        gpi = _index_col(gp, "value")
        oii = _index_col(oi, "value")
        nii = _index_col(ni, "value")
        ends = []
        fps = []
        fys = []
        gross = []
        operating = []
        net = []
        for each p in _periods(rev)
            k = _pk(p["end"], p["fp"])
            r = _get(revi, k)
            append(ends, p["end"])
            append(fps, p["fp"])
            append(fys, p["fy"])
            append(gross, _div(_get(gpi, k), r))
            append(operating, _div(_get(oii, k), r))
            append(net, _div(_get(nii, k), r))
        end for
        out = {}
        out["end"] = ends
        out["fp"] = fps
        out["fy"] = fys
        out["gross"] = gross
        out["operating"] = operating
        out["net"] = net
        return out
    end function

    ' --- ratios: interest coverage, current ratio, net debt/EBITDA, FCF conv --
    function ratios(facts)
        oi = series(facts, "operating_income")
        ie = series(facts, "interest_expense")
        ca = series(facts, "current_assets")
        cl = series(facts, "current_liabilities")
        da = series(facts, "dep_amort")
        ni = series(facts, "net_income")
        oii = _index_col(oi, "value")
        iei = _index_col(ie, "value")
        cai = _index_col(ca, "value")
        cli = _index_col(cl, "value")
        dai = _index_col(da, "value")
        nii = _index_col(ni, "value")
        neti = _index_col(debt(facts), "net")
        fcfi = _index_col(fcf(facts), "value")
        ends = []
        fps = []
        fys = []
        coverage = []
        current_ratio = []
        nd_ebitda = []
        fcf_conv = []
        for each p in _periods(ni)
            k = _pk(p["end"], p["fp"])
            ebitda = _add(_get(oii, k), _get(dai, k))
            append(ends, p["end"])
            append(fps, p["fp"])
            append(fys, p["fy"])
            append(coverage, _div(_get(oii, k), _get(iei, k)))
            append(current_ratio, _div(_get(cai, k), _get(cli, k)))
            append(nd_ebitda, _div(_get(neti, k), ebitda))
            append(fcf_conv, _div(_get(fcfi, k), _get(nii, k)))
        end for
        out = {}
        out["end"] = ends
        out["fp"] = fps
        out["fy"] = fys
        out["interest_coverage"] = coverage
        out["current_ratio"] = current_ratio
        out["net_debt_ebitda"] = nd_ebitda
        out["fcf_conversion"] = fcf_conv
        return out
    end function

    ' --- latest fiscal-year (fp="FY") value of a frame column, or unknown -----
    function _latest_fy(frame, col)
        best_end = ""
        best = unknown
        i = 0
        while i < count(frame["end"])
            if frame["fp"][i] = "FY" then
                if frame["end"][i] > best_end then
                    best_end = frame["end"][i]
                    best = frame[col][i]
                end if
            end if
            i = i + 1
        end while
        return best
    end function

    ' Latest-FY scalar for one metric key: a base concept (via series) or a named
    ' derived scalar. Unknown when the underlying data is absent.
    function _metric_latest(facts, key)
        if key = "fcf" then
            return _latest_fy(fcf(facts), "value")
        end if
        if key = "net_debt" then
            return _latest_fy(debt(facts), "net")
        end if
        if key = "gross_margin" then
            return _latest_fy(margins(facts), "gross")
        end if
        if key = "operating_margin" then
            return _latest_fy(margins(facts), "operating")
        end if
        if key = "net_margin" then
            return _latest_fy(margins(facts), "net")
        end if
        if key = "current_ratio" then
            return _latest_fy(ratios(facts), "current_ratio")
        end if
        s = series(facts, key)
        if is_unknown(s) then
            return unknown
        end if
        return _latest_fy(s, "value")
    end function

    ' --- public: compare — peer frame, one row per company -------------------
    ' facts_list: a list of decoded companyfacts records (edgar.company_facts).
    ' metrics: a list of metric keys (base concepts like "revenue"/"net_income"
    '   or derived scalars: fcf, net_debt, gross_margin, operating_margin,
    '   net_margin, current_ratio). Each cell is that company's LATEST fiscal-
    '   year value, `unknown` where the concept is absent (NA policy). Output
    '   columns: company (entityName) + one per metric.
    function compare(facts_list, metrics)
        out = {}
        out["company"] = []
        for each key in metrics
            out[key] = []
        end for
        for each facts in facts_list
            name = "unknown"
            if has(facts, "entityName") then
                name = facts.entityName
            end if
            append(out["company"], name)
            for each key in metrics
                append(out[key], _metric_latest(facts, key))
            end for
        end for
        return out
    end function
end library
