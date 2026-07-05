' WP-FOR-3 — forensics.altman (Z" + classic Z) and forensics.dilution, offline.
' The AAPL Z" block (component ratios + ingredients) is the hand-check anchor;
' classic Z is exercised with a synthetic price frame; dilution is checked on
' AAPL's large-buyback signal shape.

function load_facts(path)
    ref(file)= path
    return decode(join(read_lines(ref), "\n"))
end function

function last_idx(frame)
    return count(frame["end"]) - 1
end function

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

function r4(v)
    if is_unknown(v) then
        return "unknown"
    end if
    return string(round(v, 4))
end function

program main(args)
    load forensics from "../stdlib/forensics.bas"
    load fundamentals from "../stdlib/fundamentals.bas"

    aapl = load_facts("examples/fixtures/edgar/companyfacts_CIK0000320193.json")

    ' --- Altman Z" (book equity) — hand-check anchor ---
    z = forensics.altman(aapl)
    zi = last_idx(z)
    e = z["end"][zi]
    print("altman Z\" AAPL " + e)
    print("  x1_wc=" + r4(z["x1_working_capital"][zi]) + " x2_re=" + r4(z["x2_retained_earnings"][zi]) + " x3_ebit=" + r4(z["x3_ebit"][zi]) + " x4_bookeq=" + r4(z["x4_book_equity"][zi]))
    print("  zscore=" + r4(z["zscore"][zi]) + " zone=" + z["zone"][zi])
    print("  ingredients " + e + ": ca=" + string(fy_at(aapl,"current_assets",e)) + " cl=" + string(fy_at(aapl,"current_liabilities",e)) + " ta=" + string(fy_at(aapl,"total_assets",e)))
    print("    re=" + string(fy_at(aapl,"retained_earnings",e)) + " ebit=" + string(fy_at(aapl,"operating_income",e)) + " book_equity=" + string(fy_at(aapl,"book_equity",e)) + " total_liabilities=" + string(fy_at(aapl,"total_liabilities",e)))

    ' --- classic Z with a synthetic price frame (close = 250 at the FY end) ---
    prices = {}
    prices["date"] = [e]
    prices["close"] = [250.0]
    zc = forensics.altman_classic(aapl, prices)
    zci = last_idx(zc)
    print("altman classic Z AAPL " + e + " (synthetic close=250)")
    print("  x4_mkt=" + r4(zc["x4_market_equity"][zci]) + " x5_sales=" + r4(zc["x5_sales"][zci]) + " zscore=" + r4(zc["zscore"][zci]) + " zone=" + zc["zone"][zci])

    ' --- dilution: AAPL's large-buyback signal shape ---
    d = forensics.dilution(aapl)
    di = last_idx(d)
    print("dilution AAPL " + d["end"][di] + " prior=" + d["prior_end"][di])
    print("  shares=" + string(d["shares"][di]) + " prior_shares=" + string(d["prior_shares"][di]) + " shares_change=" + string(d["shares_change"][di]))
    print("  sbc=" + string(d["sbc"][di]) + " buybacks=" + string(d["buybacks"][di]) + " net=" + string(d["net"][di]))
end program
