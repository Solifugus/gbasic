' WP-FOR-1 — forensics.accruals + forensics.piotroski, offline against fixtures.
' The AAPL latest-fiscal-year Piotroski block (nine components + the cur/prior
' ingredient values) is the hand-check anchor Matthew verifies against the 10-K.

function load_facts(path)
    ref{file}= path
    return decode(join(read_lines(ref), "\n"))
end function

function last_idx(frame)
    return count(frame["end"]) - 1
end function

' FY value of a concept at a specific fiscal-year-end (or unknown)
function fy_at(facts, concept, endv)
    s = fundamentals.series(facts, concept)
    if is_unknown(s) then
        return unknown
    end if
    i = 0
    while i < count(s["end"])
        if s["end"][i] = endv then
            if s["fp"][i] = "FY" then
                return s["value"][i]
            end if
        end if
        i = i + 1
    end while
    return unknown
end function

function fmt(v)
    if is_unknown(v) then
        return "unknown"
    end if
    return string(v)
end function

program main(args)
    load forensics from "../stdlib/forensics.bas"
    load fundamentals from "../stdlib/fundamentals.bas"

    aapl = load_facts("examples/fixtures/edgar/companyfacts_CIK0000320193.json")
    jpm  = load_facts("examples/fixtures/edgar/companyfacts_CIK0000019617.json")

    ' --- accruals (AAPL latest FY) ---
    a = forensics.accruals(aapl)
    ai = last_idx(a)
    print("accruals AAPL " + a["end"][ai] + " prior=" + a["prior_end"][ai])
    print("  net_income=" + fmt(a["net_income"][ai]) + " operating_cash_flow=" + fmt(a["operating_cash_flow"][ai]) + " avg_total_assets=" + fmt(a["avg_total_assets"][ai]))
    print("  accrual_ratio=" + string(round(a["accrual_ratio"][ai], 6)))

    ' --- piotroski (AAPL latest FY) — the hand-check anchor ---
    p = forensics.piotroski(aapl)
    pi = last_idx(p)
    cur = p["end"][pi]
    prior = p["prior_end"][pi]
    print("piotroski AAPL " + cur + " prior=" + prior)
    print("  f_roa=" + fmt(p["f_roa"][pi]) + " f_cfo=" + fmt(p["f_cfo"][pi]) + " f_droa=" + fmt(p["f_droa"][pi]) + " f_accrual=" + fmt(p["f_accrual"][pi]))
    print("  f_dlever=" + fmt(p["f_dlever"][pi]) + " f_dliquid=" + fmt(p["f_dliquid"][pi]) + " f_shares=" + fmt(p["f_shares"][pi]))
    print("  f_dmargin=" + fmt(p["f_dmargin"][pi]) + " f_dturn=" + fmt(p["f_dturn"][pi]))
    print("  f_score=" + fmt(p["f_score"][pi]))

    ' ingredients for the hand-check (cur then prior)
    print("  ingredients cur " + cur + ": ni=" + fmt(fy_at(aapl,"net_income",cur)) + " cfo=" + fmt(fy_at(aapl,"operating_cash_flow",cur)) + " ta=" + fmt(fy_at(aapl,"total_assets",cur)) + " ltd=" + fmt(fy_at(aapl,"long_term_debt",cur)))
    print("    ca=" + fmt(fy_at(aapl,"current_assets",cur)) + " cl=" + fmt(fy_at(aapl,"current_liabilities",cur)) + " shares=" + fmt(fy_at(aapl,"shares_outstanding",cur)) + " gp=" + fmt(fy_at(aapl,"gross_profit",cur)) + " rev=" + fmt(fy_at(aapl,"revenue",cur)))
    print("  ingredients prior " + prior + ": ni=" + fmt(fy_at(aapl,"net_income",prior)) + " cfo=" + fmt(fy_at(aapl,"operating_cash_flow",prior)) + " ta=" + fmt(fy_at(aapl,"total_assets",prior)) + " ltd=" + fmt(fy_at(aapl,"long_term_debt",prior)))
    print("    ca=" + fmt(fy_at(aapl,"current_assets",prior)) + " cl=" + fmt(fy_at(aapl,"current_liabilities",prior)) + " shares=" + fmt(fy_at(aapl,"shares_outstanding",prior)) + " gp=" + fmt(fy_at(aapl,"gross_profit",prior)) + " rev=" + fmt(fy_at(aapl,"revenue",prior)))

    ' --- NA propagation (JPM, a bank): no gross_profit -> f_dmargin unknown ->
    ' f_score unknown; but the computable components still report. ---
    jp = forensics.piotroski(jpm)
    ji = last_idx(jp)
    print("piotroski JPM " + jp["end"][ji] + ": f_cfo=" + fmt(jp["f_cfo"][ji]) + " f_dmargin=" + fmt(jp["f_dmargin"][ji]) + " f_dturn=" + fmt(jp["f_dturn"][ji]) + " f_score=" + fmt(jp["f_score"][ji]))
end program
