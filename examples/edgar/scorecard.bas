' ============================================================================
' WP-DEMO-1 — "the Adrian demo": ticker in, forensic dossier out.
'
' One ticker, one screenful: multi-year fundamentals trends, the full §4.5
' forensic scorecard (earnings quality, manipulation, distress, dilution), and
' the composite red-flag list — composed entirely from the EDGAR JSON APIs, no
' vendor data, no prices, no LLM. Every number is either a fact or an honest
' `n/a`; the scores are evidence, never a verdict.
'
' Runs OFFLINE by default against captured fixtures (examples/fixtures/edgar).
' To run it LIVE against SEC, set your contact string first (SEC requires it):
'   EDGAR_IDENT="Your Name <you@example.com>" GBASIC_PATH=stdlib \
'       ./gbasic examples/edgar/scorecard.bas AAPL
' ============================================================================

' --- formatting: every value may be `unknown` -> print a clean "n/a" ---------
function money_b(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v / 1000000000, 2)) + "B"
end function

function pct(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v * 100, 1)) + "%"
end function

function num2(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v, 2))
end function

function num3(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v, 3))
end function

function show(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(v)
end function

' --- frame helpers: fundamentals frames interleave FY and quarterly rows -----
' the FY-end dates, ascending (frames are already end-ascending).
function fy_ends(frame)
    ends = []
    k = 0
    while k < count(frame["fp"])
        if frame["fp"][k] = "FY" then
            append(ends, frame["end"][k])
        end if
        k = k + 1
    end while
    return ends
end function

' FY-end -> value map for one column (bracket read of a missing end -> unknown).
function by_end(frame, col)
    m = {}
    k = 0
    while k < count(frame["fp"])
        if frame["fp"][k] = "FY" then
            m[frame["end"][k]] = frame[col][k]
        end if
        k = k + 1
    end while
    return m
end function

' latest value of a column in an FY-only forensic frame (last row = latest year).
function last(frame, col)
    c = frame[col]
    n = count(c)
    if n = 0 then
        return unknown
    end if
    return c[n - 1]
end function

program main(args)
    load edgar from "../../stdlib/edgar.bas"
    load fundamentals from "../../stdlib/fundamentals.bas"
    load forensics from "../../stdlib/forensics.bas"

    ' --- the ticker to profile (change this, or resolve any other) -----------
    ticker = "AAPL"

    ' --- wiring: offline by default, online when EDGAR_IDENT is set ----------
    cache_ref(file)= "examples/tmp_scorecard_cache.db"
    if exists(cache_ref) then
        delete(cache_ref)
    end if
    contact = env("EDGAR_IDENT")
    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_scorecard_cache.db")
    if is_unknown(contact) then
        e = edgar.identify(e, "offline-demo")
        e = edgar.offline(e, "examples/fixtures/edgar")
    else
        e = edgar.identify(e, contact)
    end if

    ' --- resolve + fetch -----------------------------------------------------
    cik = edgar.cik(e, ticker)
    if is_unknown(cik) then
        print("ticker not found: " + ticker)
        return
    end if
    facts = edgar.company_facts(e, cik)
    subs = edgar.submissions(e, cik)

    print("================================================================")
    print("  FORENSIC DOSSIER — " + ticker + "  (" + facts["entityName"] + ")")
    print("  CIK " + cik)
    print("================================================================")

    ' --- fundamentals trends: FCF, margins, net debt (last 4 fiscal years) ---
    mg = fundamentals.margins(facts)
    fc = fundamentals.fcf(facts)
    db = fundamentals.debt(facts)
    ends = fy_ends(mg)
    fcf_by = by_end(fc, "value")
    g_by = by_end(mg, "gross")
    o_by = by_end(mg, "operating")
    n_by = by_end(mg, "net")
    nd_by = by_end(db, "net")

    print("")
    print("-- fundamentals (fiscal year) ----------------------------------")
    print("  end          FCF       gross    oper     net      net debt")
    start = count(ends) - 4
    if start < 0 then
        start = 0
    end if
    i = start
    while i < count(ends)
        d = ends[i]
        print("  " + d + "  " + money_b(fcf_by[d]) + "   " + pct(g_by[d]) + "  " + pct(o_by[d]) + "  " + pct(n_by[d]) + "  " + money_b(nd_by[d]))
        i = i + 1
    end while

    ' --- the §4.5 forensic scorecard (latest fiscal year) --------------------
    acc = forensics.accruals(facts)
    be = forensics.beneish(facts)
    pt = forensics.piotroski(facts)
    al = forensics.altman(facts)
    di = forensics.dilution(facts)

    print("")
    print("-- forensic scorecard (latest fiscal year) ---------------------")
    print("  Piotroski F-Score   " + show(last(pt, "f_score")) + " / 9        (fundamental health, higher better)")
    print("  Beneish M-Score     " + num2(last(be, "mscore")) + "         (manipulation; flag=" + show(last(be, "flag")) + ", threshold -1.78)")
    print("  Altman Z\"           " + num2(last(al, "zscore")) + "         (distress; zone=" + show(last(al, "zone")) + ")")
    print("  Sloan accrual ratio " + num3(last(acc, "accrual_ratio")) + "        (earnings quality; lower/negative better)")
    print("  Dilution net        " + money_b(last(di, "net")) + "      (buybacks minus stock-based comp)")

    ' --- composite red flags (evidence, not verdicts) ------------------------
    fl = forensics.flags(facts, subs)
    print("")
    print("-- red flags (composite; signals, not verdicts) ----------------")
    kinds = ["nt_filing", "amendment", "auditor_change", "non_reliance", "officer_exodus", "rising_accruals", "mscore_flag", "positive_ni_negative_fcf"]
    total = count(fl["kind"])
    for each kind in kinds
        c = 0
        for each k in fl["kind"]
            if k = kind then
                c = c + 1
            end if
        end for
        if c > 0 then
            print("  " + kind + " x" + string(c))
        end if
    end for
    if total = 0 then
        print("  (none)")
    end if
    print("  total: " + string(total) + " flag rows")
    print("")
    print("  note: forensic scores are published models over reported facts;")
    print("  Beneish/Altman were built on industrials and are known to")
    print("  false-positive on large growth+buyback firms. Read as evidence.")

    if exists(cache_ref) then
        delete(cache_ref)
    end if
end program
