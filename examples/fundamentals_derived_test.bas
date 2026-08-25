' WP-EDG-5 — fundamentals derived metrics: fcf / debt / margins / ratios,
' offline against fixtures. The FCF hand-check anchor (AAPL, one fiscal year,
' with both ingredients) is the decisive value Matthew verifies against the 10-K.
' `unknown` is displayed verbatim so NA propagation is visible (never masked).

function load_facts(path)
    ref{file}= path
    return decode(join(read_lines(ref), "\n"))
end function

' latest fiscal-year (fp="FY") value of a frame column, or unknown
function fy_val(frame, col)
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

function fy_end(frame)
    best_end = ""
    i = 0
    while i < count(frame["end"])
        if frame["fp"][i] = "FY" then
            if frame["end"][i] > best_end then
                best_end = frame["end"][i]
            end if
        end if
        i = i + 1
    end while
    return best_end
end function

function fmt(v)
    if is_unknown(v) then
        return "unknown"
    end if
    return string(v)
end function

function fmt4(v)
    if is_unknown(v) then
        return "unknown"
    end if
    return string(round(v, 4))
end function

program main(args)
    load fundamentals from "../stdlib/fundamentals.bas"

    aapl = load_facts("examples/fixtures/edgar/companyfacts_CIK0000320193.json")
    jpm  = load_facts("examples/fixtures/edgar/companyfacts_CIK0000019617.json")

    ' --- FCF hand-check anchor (AAPL latest FY): fcf = ocf - capex ---
    ocf = fundamentals.series(aapl, "operating_cash_flow")
    cap = fundamentals.series(aapl, "capex")
    f = fundamentals.fcf(aapl)
    e = fy_end(f)
    print("fcf AAPL " + e + ": ocf=" + fmt(fy_val(ocf, "value")) + " capex=" + fmt(fy_val(cap, "value")) + " fcf=" + fmt(fy_val(f, "value")))

    ' --- margins (AAPL) ---
    m = fundamentals.margins(aapl)
    print("margins AAPL " + fy_end(m) + ": gross=" + fmt4(fy_val(m, "gross")) + " op=" + fmt4(fy_val(m, "operating")) + " net=" + fmt4(fy_val(m, "net")))

    ' --- debt (AAPL) ---
    d = fundamentals.debt(aapl)
    print("debt AAPL " + fy_end(d) + ": total=" + fmt(fy_val(d, "total")) + " noncurrent=" + fmt(fy_val(d, "noncurrent")) + " net=" + fmt(fy_val(d, "net")))

    ' --- ratios (AAPL) — coverage is unknown (Apple stopped reporting interest
    ' expense after FY2023): NA propagates, not masked. ---
    r = fundamentals.ratios(aapl)
    print("ratios AAPL " + fy_end(r) + ": coverage=" + fmt4(fy_val(r, "interest_coverage")) + " current_ratio=" + fmt4(fy_val(r, "current_ratio")) + " nd_ebitda=" + fmt4(fy_val(r, "net_debt_ebitda")) + " fcf_conv=" + fmt4(fy_val(r, "fcf_conversion")))

    ' --- NA propagation (JPM, a bank): no capex -> fcf unknown; no gross/
    ' operating income -> those margins unknown, net margin present. ---
    jf = fundamentals.fcf(jpm)
    jm = fundamentals.margins(jpm)
    print("fcf JPM " + fy_end(jf) + ": " + fmt(fy_val(jf, "value")))
    print("margins JPM " + fy_end(jm) + ": gross=" + fmt4(fy_val(jm, "gross")) + " op=" + fmt4(fy_val(jm, "operating")) + " net=" + fmt4(fy_val(jm, "net")))
end program
