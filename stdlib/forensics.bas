' forensics.bas — the honesty arithmetic (edgar_design.md §4.5, WP-FOR-1).
'
' Published, citable earnings-quality models computed entirely from §4.2's
' concept map (fundamentals.series) — no vendor data, no prices, no LLM. Each
' function returns a SCORECARD FRAME (one row per fiscal year), components
' exposed alongside the headline, and `unknown` wherever an ingredient is
' missing (the standing NA policy).
'
' Scorecard-frame conventions (shared by every score here and in WP-FOR-2..4):
'   * period columns: `end` (fiscal year end) and `prior_end` (the year compared
'     against — these are year-over-year models),
'   * one column per component (ingredient or boolean test),
'   * one headline column (accrual_ratio / f_score),
'   * a component or the headline is `unknown` when an ingredient is absent.
library forensics
    load fundamentals from "fundamentals.bas"

    ' --- NA-propagating helpers ---------------------------------------------
    function _fget(idx, k)
        if has(idx, k) then
            return idx[k]
        end if
        return unknown
    end function

    function _fsub(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return a - b
    end function

    function _avg(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return (a + b) / 2
    end function

    function _ratio(a, b)
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

    ' comparisons -> 1 / 0 / unknown
    function _gt(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        if a > b then
            return 1
        end if
        return 0
    end function

    function _lt(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        if a < b then
            return 1
        end if
        return 0
    end function

    function _le(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        if a <= b then
            return 1
        end if
        return 0
    end function

    ' sum of a component list, or unknown if any component is unknown
    function _score(components)
        total = 0
        for each v in components
            if is_unknown(v) then
                return unknown
            end if
            total = total + v
        end for
        return total
    end function

    ' --- per-fiscal-year value index for a concept: end -> value -------------
    function _fy_index(facts, concept)
        s = fundamentals.series(facts, concept)
        idx = {}
        if is_unknown(s) then
            return idx
        end if
        i = 0
        while i < count(s["end"])
            if s["fp"][i] = "FY" then
                idx[s["end"][i]] = s["value"][i]
            end if
            i = i + 1
        end while
        return idx
    end function

    ' sorted, unique fiscal-year end dates (driver = total_assets, present for
    ' every filer; the deduped series is already end-ascending and one row per
    ' fiscal year end)
    function _annual_ends(facts)
        s = fundamentals.series(facts, "total_assets")
        ends = []
        if is_unknown(s) then
            return ends
        end if
        i = 0
        while i < count(s["end"])
            if s["fp"][i] = "FY" then
                append(ends, s["end"][i])
            end if
            i = i + 1
        end while
        return ends
    end function

    ' --- accruals: Sloan ratio = (net income - operating cash flow) / average
    ' total assets, per fiscal year (average = (assets_t + assets_{t-1})/2) -----
    function accruals(facts)
        ni = _fy_index(facts, "net_income")
        cfo = _fy_index(facts, "operating_cash_flow")
        ta = _fy_index(facts, "total_assets")
        ends = _annual_ends(facts)
        c_end = []
        c_prior = []
        c_ni = []
        c_cfo = []
        c_avg = []
        c_ratio = []
        k = 1
        while k < count(ends)
            cur = ends[k]
            prev = ends[k - 1]
            niv = _fget(ni, cur)
            cfov = _fget(cfo, cur)
            avg = _avg(_fget(ta, cur), _fget(ta, prev))
            append(c_end, cur)
            append(c_prior, prev)
            append(c_ni, niv)
            append(c_cfo, cfov)
            append(c_avg, avg)
            append(c_ratio, _ratio(_fsub(niv, cfov), avg))
            k = k + 1
        end while
        out = {}
        out["end"] = c_end
        out["prior_end"] = c_prior
        out["net_income"] = c_ni
        out["operating_cash_flow"] = c_cfo
        out["avg_total_assets"] = c_avg
        out["accrual_ratio"] = c_ratio
        return out
    end function

    function _fadd(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return a + b
    end function

    ' m > threshold as a boolean, NA-propagating.
    function _over(m, thr)
        if is_unknown(m) then
            return unknown
        end if
        return m > thr
    end function

    ' Beneish (1999) index weights, in the order [DSRI, GMI, AQI, SGI, DEPI, SGAI,
    ' TATA, LVGI].
    function _beneish_coeffs()
        return [0.920, 0.528, 0.404, 0.892, 0.115, -0.172, 4.679, -0.327]
    end function

    ' M-Score from a record of the eight indices { dsri, gmi, aqi, sgi, depi,
    ' sgai, lvgi, tata }. `unknown` if any index is unknown. Public so callers who
    ' already hold indices (or want to reweight) can score directly.
    function mscore(indices)
        xs = [indices["dsri"], indices["gmi"], indices["aqi"], indices["sgi"], indices["depi"], indices["sgai"], indices["tata"], indices["lvgi"]]
        return _fadd(-4.84, _zscore(_beneish_coeffs(), xs))
    end function

    ' --- Beneish M-Score: eight-index earnings-manipulation model (WP-FOR-2) ---
    ' Coefficients and the -1.78 flag threshold are Beneish (1999), "The Detection
    ' of Earnings Manipulation" (Financial Analysts Journal 55(5)). Goldened against
    ' a published worked example (Boeing FY2023). Each index is a year-over-year
    ' ratio; a missing ingredient makes that index -- and therefore mscore/flag --
    ' `unknown` (NA policy). Indices are scale-invariant (ratios of ratios), so the
    ' fact units do not matter as long as they are consistent.
    '   DSRI = (Recv_t/Sales_t)/(Recv_p/Sales_p)
    '   GMI  = GM_p/GM_t,  GM = (Sales-COGS)/Sales
    '   AQI  = (1-(CA_t+PPE_t)/TA_t)/(1-(CA_p+PPE_p)/TA_p)
    '   SGI  = Sales_t/Sales_p
    '   DEPI = (Dep_p/(Dep_p+PPE_p))/(Dep_t/(Dep_t+PPE_t))
    '   SGAI = (SGA_t/Sales_t)/(SGA_p/Sales_p)
    '   LVGI = ((LTD_t+CL_t)/TA_t)/((LTD_p+CL_p)/TA_p)
    '   TATA = (NI_t-CFO_t)/TA_t
    '   M = -4.84 + 0.920 DSRI + 0.528 GMI + 0.404 AQI + 0.892 SGI
    '            + 0.115 DEPI - 0.172 SGAI + 4.679 TATA - 0.327 LVGI
    function beneish(facts)
        recv = _fy_index(facts, "receivables")
        sales = _fy_index(facts, "revenue")
        cogs = _fy_index(facts, "cost_of_revenue")
        ca = _fy_index(facts, "current_assets")
        ppe = _fy_index(facts, "ppe_net")
        ta = _fy_index(facts, "total_assets")
        dep = _fy_index(facts, "dep_amort")
        sga = _fy_index(facts, "sga")
        cl = _fy_index(facts, "current_liabilities")
        ltd = _fy_index(facts, "long_term_debt")
        ni = _fy_index(facts, "net_income")
        cfo = _fy_index(facts, "operating_cash_flow")
        ends = _annual_ends(facts)

        c_end = []
        c_prior = []
        c_dsri = []
        c_gmi = []
        c_aqi = []
        c_sgi = []
        c_depi = []
        c_sgai = []
        c_lvgi = []
        c_tata = []
        c_m = []
        c_flag = []

        k = 1
        while k < count(ends)
            cur = ends[k]
            prev = ends[k - 1]

            recv_t = _fget(recv, cur)
            recv_p = _fget(recv, prev)
            sales_t = _fget(sales, cur)
            sales_p = _fget(sales, prev)
            cogs_t = _fget(cogs, cur)
            cogs_p = _fget(cogs, prev)
            ca_t = _fget(ca, cur)
            ca_p = _fget(ca, prev)
            ppe_t = _fget(ppe, cur)
            ppe_p = _fget(ppe, prev)
            ta_t = _fget(ta, cur)
            ta_p = _fget(ta, prev)
            dep_t = _fget(dep, cur)
            dep_p = _fget(dep, prev)
            sga_t = _fget(sga, cur)
            sga_p = _fget(sga, prev)
            cl_t = _fget(cl, cur)
            cl_p = _fget(cl, prev)
            ltd_t = _fget(ltd, cur)
            ltd_p = _fget(ltd, prev)
            ni_t = _fget(ni, cur)
            cfo_t = _fget(cfo, cur)

            dsri = _ratio(_ratio(recv_t, sales_t), _ratio(recv_p, sales_p))
            gm_t = _ratio(_fsub(sales_t, cogs_t), sales_t)
            gm_p = _ratio(_fsub(sales_p, cogs_p), sales_p)
            gmi = _ratio(gm_p, gm_t)
            aq_t = _fsub(1, _ratio(_fadd(ca_t, ppe_t), ta_t))
            aq_p = _fsub(1, _ratio(_fadd(ca_p, ppe_p), ta_p))
            aqi = _ratio(aq_t, aq_p)
            sgi = _ratio(sales_t, sales_p)
            dr_t = _ratio(dep_t, _fadd(dep_t, ppe_t))
            dr_p = _ratio(dep_p, _fadd(dep_p, ppe_p))
            depi = _ratio(dr_p, dr_t)
            sgai = _ratio(_ratio(sga_t, sales_t), _ratio(sga_p, sales_p))
            lvgi = _ratio(_ratio(_fadd(ltd_t, cl_t), ta_t), _ratio(_fadd(ltd_p, cl_p), ta_p))
            tata = _ratio(_fsub(ni_t, cfo_t), ta_t)

            m = mscore({ dsri: dsri, gmi: gmi, aqi: aqi, sgi: sgi, depi: depi, sgai: sgai, lvgi: lvgi, tata: tata })

            append(c_end, cur)
            append(c_prior, prev)
            append(c_dsri, dsri)
            append(c_gmi, gmi)
            append(c_aqi, aqi)
            append(c_sgi, sgi)
            append(c_depi, depi)
            append(c_sgai, sgai)
            append(c_lvgi, lvgi)
            append(c_tata, tata)
            append(c_m, m)
            append(c_flag, _over(m, -1.78))
            k = k + 1
        end while

        out = {}
        out["end"] = c_end
        out["prior_end"] = c_prior
        out["dsri"] = c_dsri
        out["gmi"] = c_gmi
        out["aqi"] = c_aqi
        out["sgi"] = c_sgi
        out["depi"] = c_depi
        out["sgai"] = c_sgai
        out["lvgi"] = c_lvgi
        out["tata"] = c_tata
        out["mscore"] = c_m
        out["flag"] = c_flag
        return out
    end function

    ' --- Piotroski F-Score: nine binary fundamental-health tests, summed 0-9 ---
    ' Standard Piotroski (2000). ROA uses year-end total assets (a common
    ' variant of the original beginning-of-year denominator; the sign and
    ' direction tests are robust to the choice). f_score is `unknown` when any
    ' of the nine components is unknown — a partial F-Score is not an F-Score.
    function piotroski(facts)
        ni = _fy_index(facts, "net_income")
        cfo = _fy_index(facts, "operating_cash_flow")
        ta = _fy_index(facts, "total_assets")
        ltd = _fy_index(facts, "long_term_debt")
        ca = _fy_index(facts, "current_assets")
        cl = _fy_index(facts, "current_liabilities")
        sh = _fy_index(facts, "shares_outstanding")
        gp = _fy_index(facts, "gross_profit")
        rev = _fy_index(facts, "revenue")
        ends = _annual_ends(facts)

        c_end = []
        c_prior = []
        f1 = []
        f2 = []
        f3 = []
        f4 = []
        f5 = []
        f6 = []
        f7 = []
        f8 = []
        f9 = []
        c_score = []

        k = 1
        while k < count(ends)
            cur = ends[k]
            prev = ends[k - 1]

            ni_c = _fget(ni, cur)
            ni_p = _fget(ni, prev)
            cfo_c = _fget(cfo, cur)
            ta_c = _fget(ta, cur)
            ta_p = _fget(ta, prev)

            ' profitability
            roa_c = _ratio(ni_c, ta_c)
            roa_p = _ratio(ni_p, ta_p)
            f_roa = _gt(roa_c, 0)
            f_cfo = _gt(cfo_c, 0)
            f_droa = _gt(roa_c, roa_p)
            f_accrual = _gt(cfo_c, ni_c)

            ' leverage / liquidity / shares
            lev_c = _ratio(_fget(ltd, cur), ta_c)
            lev_p = _ratio(_fget(ltd, prev), ta_p)
            f_dlever = _lt(lev_c, lev_p)
            cr_c = _ratio(_fget(ca, cur), _fget(cl, cur))
            cr_p = _ratio(_fget(ca, prev), _fget(cl, prev))
            f_dliquid = _gt(cr_c, cr_p)
            f_shares = _le(_fget(sh, cur), _fget(sh, prev))

            ' operating efficiency
            gm_c = _ratio(_fget(gp, cur), _fget(rev, cur))
            gm_p = _ratio(_fget(gp, prev), _fget(rev, prev))
            f_dmargin = _gt(gm_c, gm_p)
            turn_c = _ratio(_fget(rev, cur), ta_c)
            turn_p = _ratio(_fget(rev, prev), ta_p)
            f_dturn = _gt(turn_c, turn_p)

            comps = [f_roa, f_cfo, f_droa, f_accrual, f_dlever, f_dliquid, f_shares, f_dmargin, f_dturn]

            append(c_end, cur)
            append(c_prior, prev)
            append(f1, f_roa)
            append(f2, f_cfo)
            append(f3, f_droa)
            append(f4, f_accrual)
            append(f5, f_dlever)
            append(f6, f_dliquid)
            append(f7, f_shares)
            append(f8, f_dmargin)
            append(f9, f_dturn)
            append(c_score, _score(comps))
            k = k + 1
        end while

        out = {}
        out["end"] = c_end
        out["prior_end"] = c_prior
        out["f_roa"] = f1
        out["f_cfo"] = f2
        out["f_droa"] = f3
        out["f_accrual"] = f4
        out["f_dlever"] = f5
        out["f_dliquid"] = f6
        out["f_shares"] = f7
        out["f_dmargin"] = f8
        out["f_dturn"] = f9
        out["f_score"] = c_score
        return out
    end function

    ' --- shared helpers for Altman / dilution --------------------------------
    function _fmul(a, b)
        if is_unknown(a) then
            return unknown
        end if
        if is_unknown(b) then
            return unknown
        end if
        return a * b
    end function

    ' weighted sum of ratios; unknown if any ratio is unknown
    function _zscore(coeffs, xs)
        total = 0
        i = 0
        while i < count(xs)
            if is_unknown(xs[i]) then
                return unknown
            end if
            total = total + coeffs[i] * xs[i]
            i = i + 1
        end while
        return total
    end function

    ' zone classification: below lo => distress, above hi => safe, else grey
    function _zone(z, lo, hi)
        if is_unknown(z) then
            return unknown
        end if
        if z < lo then
            return "distress"
        end if
        if z > hi then
            return "safe"
        end if
        return "grey"
    end function

    ' close price at an exact date from a { date:[...], close:[...] } frame
    function _price_at(prices, date)
        if is_unknown(prices) then
            return unknown
        end if
        i = 0
        while i < count(prices["date"])
            if prices["date"][i] = date then
                return prices["close"][i]
            end if
            i = i + 1
        end while
        return unknown
    end function

    ' --- Altman Z-Score, Z" variant (book equity; no market data) ------------
    ' Z" = 6.56*X1 + 3.26*X2 + 6.72*X3 + 1.05*X4  (Altman 1995). Per fiscal year.
    ' X1 = working capital / total assets, X2 = retained earnings / total assets,
    ' X3 = EBIT / total assets, X4 = BOOK equity / total liabilities.
    ' Zones: >2.6 safe, 1.1-2.6 grey, <1.1 distress.
    function altman(facts)
        ca = _fy_index(facts, "current_assets")
        cl = _fy_index(facts, "current_liabilities")
        ta = _fy_index(facts, "total_assets")
        re = _fy_index(facts, "retained_earnings")
        oi = _fy_index(facts, "operating_income")
        tl = _fy_index(facts, "total_liabilities")
        be = _fy_index(facts, "book_equity")
        ends = _annual_ends(facts)
        c_end = []
        cx1 = []
        cx2 = []
        cx3 = []
        cx4 = []
        cz = []
        czone = []
        for each e in ends
            taE = _fget(ta, e)
            x1 = _ratio(_fsub(_fget(ca, e), _fget(cl, e)), taE)
            x2 = _ratio(_fget(re, e), taE)
            x3 = _ratio(_fget(oi, e), taE)
            x4 = _ratio(_fget(be, e), _fget(tl, e))
            z = _zscore([6.56, 3.26, 6.72, 1.05], [x1, x2, x3, x4])
            append(c_end, e)
            append(cx1, x1)
            append(cx2, x2)
            append(cx3, x3)
            append(cx4, x4)
            append(cz, z)
            append(czone, _zone(z, 1.1, 2.6))
        end for
        out = {}
        out["end"] = c_end
        out["x1_working_capital"] = cx1
        out["x2_retained_earnings"] = cx2
        out["x3_ebit"] = cx3
        out["x4_book_equity"] = cx4
        out["zscore"] = cz
        out["zone"] = czone
        return out
    end function

    ' --- Altman classic Z (1968; market equity from a price frame) -----------
    ' Z = 1.2*X1 + 1.4*X2 + 3.3*X3 + 0.6*X4 + 1.0*X5. X4 = MARKET equity /
    ' total liabilities (market equity = close price at fiscal year end x shares
    ' outstanding); X5 = sales / total assets. prices: { date:[...], close:[...] }.
    ' Zones: >2.99 safe, 1.81-2.99 grey, <1.81 distress.
    function altman_classic(facts, prices)
        ca = _fy_index(facts, "current_assets")
        cl = _fy_index(facts, "current_liabilities")
        ta = _fy_index(facts, "total_assets")
        re = _fy_index(facts, "retained_earnings")
        oi = _fy_index(facts, "operating_income")
        tl = _fy_index(facts, "total_liabilities")
        sh = _fy_index(facts, "shares_outstanding")
        rev = _fy_index(facts, "revenue")
        ends = _annual_ends(facts)
        c_end = []
        cx1 = []
        cx2 = []
        cx3 = []
        cx4 = []
        cx5 = []
        cz = []
        czone = []
        for each e in ends
            taE = _fget(ta, e)
            x1 = _ratio(_fsub(_fget(ca, e), _fget(cl, e)), taE)
            x2 = _ratio(_fget(re, e), taE)
            x3 = _ratio(_fget(oi, e), taE)
            mkteq = _fmul(_price_at(prices, e), _fget(sh, e))
            x4 = _ratio(mkteq, _fget(tl, e))
            x5 = _ratio(_fget(rev, e), taE)
            z = _zscore([1.2, 1.4, 3.3, 0.6, 1.0], [x1, x2, x3, x4, x5])
            append(c_end, e)
            append(cx1, x1)
            append(cx2, x2)
            append(cx3, x3)
            append(cx4, x4)
            append(cx5, x5)
            append(cz, z)
            append(czone, _zone(z, 1.81, 2.99))
        end for
        out = {}
        out["end"] = c_end
        out["x1_working_capital"] = cx1
        out["x2_retained_earnings"] = cx2
        out["x3_ebit"] = cx3
        out["x4_market_equity"] = cx4
        out["x5_sales"] = cx5
        out["zscore"] = cz
        out["zone"] = czone
        return out
    end function

    ' --- dilution: shares trend, SBC, buyback spend, and the net -------------
    ' net = buybacks - SBC: positive means buybacks exceed share-based comp
    ' (real capital return); near zero / negative means buybacks merely mop up
    ' the company's own stock issuance. Per fiscal year (YoY shares change).
    function dilution(facts)
        sh = _fy_index(facts, "shares_outstanding")
        sbc = _fy_index(facts, "share_based_comp")
        bb = _fy_index(facts, "stock_repurchased")
        ends = _annual_ends(facts)
        c_end = []
        c_prior = []
        c_shares = []
        c_prior_shares = []
        c_change = []
        c_sbc = []
        c_bb = []
        c_net = []
        k = 1
        while k < count(ends)
            cur = ends[k]
            prev = ends[k - 1]
            shares = _fget(sh, cur)
            prior_shares = _fget(sh, prev)
            sbcv = _fget(sbc, cur)
            bbv = _fget(bb, cur)
            append(c_end, cur)
            append(c_prior, prev)
            append(c_shares, shares)
            append(c_prior_shares, prior_shares)
            append(c_change, _fsub(shares, prior_shares))
            append(c_sbc, sbcv)
            append(c_bb, bbv)
            append(c_net, _fsub(bbv, sbcv))
            k = k + 1
        end while
        out = {}
        out["end"] = c_end
        out["prior_end"] = c_prior
        out["shares"] = c_shares
        out["prior_shares"] = c_prior_shares
        out["shares_change"] = c_change
        out["sbc"] = c_sbc
        out["buybacks"] = c_bb
        out["net"] = c_net
        return out
    end function

    ' --- flags: the composite red-flag dossier (WP-FOR-4) --------------------
    ' One frame joining the cheap trouble signals of edgar_design.md §4.5 — some
    ' from the submissions feed (`subs`, an edgar.submissions frame: parallel
    ' columns form/filed/accession/period/items, newest-first), some from the
    ' fundamentals (`facts`). Columns are evidence, not a verdict:
    '   kind      — which signal fired (see the eight detectors below)
    '   date      — the event date (filing date, or the fiscal-year end)
    '   accession — the filing's accession (submissions signals) or `unknown`
    '   period    — reportDate (submissions) or the fiscal-year end (facts)
    '   detail    — a short human-readable description
    ' Rows are emitted detector-by-detector in the fixed order below (submissions
    ' rows in feed order, facts rows fiscal-year ascending), so the frame is fully
    ' deterministic without a sort. `window_days` bounds the 5.02 officer-exodus
    ' cluster; the two-arg `flags` defaults it to 90.

    ' days since 1970-01-01 for a "YYYY-MM-DD" string (Howard Hinnant's
    ' days_from_civil; EDGAR dates are all positive years, so no negative-era
    ' handling is needed). Pure arithmetic — O(1), no day-walking.
    function _civil_days(s)
        parts = split(s, "-")
        y = number(parts[0])
        m = number(parts[1])
        d = number(parts[2])
        if m <= 2 then
            y = y - 1
        end if
        era = floor(y / 400)
        yoe = y - era * 400
        mm = m - 3
        if m <= 2 then
            mm = m + 9
        end if
        doy = floor((153 * mm + 2) / 5) + d - 1
        doe = yoe * 365 + floor(yoe / 4) - floor(yoe / 100) + doy
        return era * 146097 + doe - 719468
    end function

    ' a column from a submissions frame, or [] if absent (a real-filer run must
    ' never raise on a frame that happens to lack a column).
    function _col(subs, name)
        if has(subs, name) then
            return subs[name]
        end if
        return []
    end function

    ' is `code` (e.g. "4.02") among the comma-separated 8-K item codes in an
    ' items-cell string? No item code is a prefix of another, so starts_with on
    ' the trimmed token is safe and tolerates trailing item descriptions.
    function _item_hit(items_cell, code)
        if is_unknown(items_cell) then
            return false
        end if
        if not is_string(items_cell) then
            return false
        end if
        for each tok in split(items_cell, ",")
            if starts_with(trim(tok), code) then
                return true
            end if
        end for
        return false
    end function

    function _is_8k(form)
        if not is_string(form) then
            return false
        end if
        return starts_with(form, "8-K")
    end function

    ' the five SUBMISSIONS-side (facts-free) detectors as a standalone dossier
    ' frame. Shared by flags_window (which then appends the facts-side detectors)
    ' and exposed directly as events_window — the §7 monitor's classifier, which
    ' must raise an NT / amendment / 4.01 / 4.02 / 5.02-cluster alert on a live
    ' filing stream WITHOUT holding the filer's companyfacts.
    function _submission_flags(subs, window_days)
        k_kind = []
        k_date = []
        k_acc = []
        k_period = []
        k_detail = []

        forms = _col(subs, "form")
        filed = _col(subs, "filed")
        accs = _col(subs, "accession")
        periods = _col(subs, "period")
        items = _col(subs, "items")
        n = count(forms)

        ' safe per-row accessors (period/items may be shorter or absent)
        ' -- NT filings (cannot file on time) ---------------------------------
        i = 0
        while i < n
            form = forms[i]
            if is_string(form) and starts_with(form, "NT ") then
                append(k_kind, "nt_filing")
                append(k_date, filed[i])
                append(k_acc, accs[i])
                append(k_period, _row(periods, i))
                append(k_detail, "late filing: " + form)
            end if
            i = i + 1
        end while

        ' -- 10-K/A, 10-Q/A amendments (restatement territory) ----------------
        i = 0
        while i < n
            form = forms[i]
            if is_string(form) and (form = "10-K/A" or form = "10-Q/A") then
                append(k_kind, "amendment")
                append(k_date, filed[i])
                append(k_acc, accs[i])
                append(k_period, _row(periods, i))
                append(k_detail, "amended financials: " + form)
            end if
            i = i + 1
        end while

        ' -- 8-K item 4.01 (auditor change) -----------------------------------
        i = 0
        while i < n
            if _is_8k(forms[i]) and _item_hit(_row(items, i), "4.01") then
                append(k_kind, "auditor_change")
                append(k_date, filed[i])
                append(k_acc, accs[i])
                append(k_period, _row(periods, i))
                append(k_detail, "8-K 4.01 auditor change")
            end if
            i = i + 1
        end while

        ' -- 8-K item 4.02 (non-reliance: the fire alarm) ---------------------
        i = 0
        while i < n
            if _is_8k(forms[i]) and _item_hit(_row(items, i), "4.02") then
                append(k_kind, "non_reliance")
                append(k_date, filed[i])
                append(k_acc, accs[i])
                append(k_period, _row(periods, i))
                append(k_detail, "8-K 4.02 non-reliance on prior financials")
            end if
            i = i + 1
        end while

        ' -- clustered 8-K item 5.02 (officer exodus within window_days) ------
        cl_idx = []
        cl_day = []
        i = 0
        while i < n
            if _is_8k(forms[i]) and _item_hit(_row(items, i), "5.02") and is_string(filed[i]) then
                append(cl_idx, i)
                append(cl_day, _civil_days(filed[i]))
            end if
            i = i + 1
        end while
        a = 0
        while a < count(cl_idx)
            near = 1
            b = 0
            while b < count(cl_idx)
                if b != a then
                    if abs(cl_day[a] - cl_day[b]) <= window_days then
                        near = near + 1
                    end if
                end if
                b = b + 1
            end while
            if near >= 2 then
                ix = cl_idx[a]
                append(k_kind, "officer_exodus")
                append(k_date, filed[ix])
                append(k_acc, accs[ix])
                append(k_period, _row(periods, ix))
                append(k_detail, "5.02 officer change clustered (" + string(near) + " within " + string(window_days) + "d)")
            end if
            a = a + 1
        end while

        out = {}
        out["kind"] = k_kind
        out["date"] = k_date
        out["accession"] = k_acc
        out["period"] = k_period
        out["detail"] = k_detail
        return out
    end function

    ' events: the submissions-side red-flag surface on its own (edgar_design.md
    ' §1/§4.5) — NT / amendment / auditor-change / non-reliance / officer-exodus.
    ' Same kinds, columns, ordering, and cluster window as the submissions rows of
    ' flags(); the two-arg form defaults the window to 90 days. This is what the
    ' §7 monitor watches per poll (no companyfacts required).
    function events(subs)
        return _submission_flags(subs, 90)
    end function

    function events_window(subs, window_days)
        return _submission_flags(subs, window_days)
    end function

    function flags(facts, subs)
        return flags_window(facts, subs, 90)
    end function

    ' flags = the submissions-side events PLUS the facts-side forensic detectors
    ' (rising accruals / M-Score / positive-NI-negative-FCF), submissions rows
    ' first (feed order) then facts rows (fiscal-year ascending).
    function flags_window(facts, subs, window_days)
        sub = _submission_flags(subs, window_days)
        k_kind = sub["kind"]
        k_date = sub["date"]
        k_acc = sub["accession"]
        k_period = sub["period"]
        k_detail = sub["detail"]

        ' -- rising accruals (accrual ratio up YoY and positive) --------------
        acc = accruals(facts)
        ar = acc["accrual_ratio"]
        aends = acc["end"]
        j = 1
        while j < count(ar)
            cur_r = ar[j]
            prev_r = ar[j - 1]
            if (not is_unknown(cur_r)) and (not is_unknown(prev_r)) then
                if cur_r > prev_r and cur_r > 0 then
                    append(k_kind, "rising_accruals")
                    append(k_date, aends[j])
                    append(k_acc, unknown)
                    append(k_period, aends[j])
                    append(k_detail, "accrual ratio " + string(round(prev_r, 3)) + " -> " + string(round(cur_r, 3)))
                end if
            end if
            j = j + 1
        end while

        ' -- M-Score over the -1.78 manipulation threshold --------------------
        bn = beneish(facts)
        bfl = bn["flag"]
        j = 0
        while j < count(bfl)
            fl = bfl[j]
            if (not is_unknown(fl)) and fl = true then
                append(k_kind, "mscore_flag")
                append(k_date, bn["end"][j])
                append(k_acc, unknown)
                append(k_period, bn["end"][j])
                append(k_detail, "M-Score " + string(round(bn["mscore"][j], 2)) + " > -1.78")
            end if
            j = j + 1
        end while

        ' -- positive net income but negative free cash flow ------------------
        ni = _fy_index(facts, "net_income")
        ocf = _fy_index(facts, "operating_cash_flow")
        cap = _fy_index(facts, "capex")
        for each e in _annual_ends(facts)
            niv = _fget(ni, e)
            fcf = _fsub(_fget(ocf, e), _fget(cap, e))
            if (not is_unknown(niv)) and (not is_unknown(fcf)) then
                if niv > 0 and fcf < 0 then
                    append(k_kind, "positive_ni_negative_fcf")
                    append(k_date, e)
                    append(k_acc, unknown)
                    append(k_period, e)
                    append(k_detail, "positive net income, negative free cash flow")
                end if
            end if
        end for

        out = {}
        out["kind"] = k_kind
        out["date"] = k_date
        out["accession"] = k_acc
        out["period"] = k_period
        out["detail"] = k_detail
        return out
    end function

    ' safe row read: value at index i of column col, or `unknown` past its end.
    function _row(col, i)
        if i < count(col) then
            return col[i]
        end if
        return unknown
    end function
end library
