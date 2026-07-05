' fundamentals_demo.bas — WP-EDG-6 worked example. The whole Track A numeric
' pipeline on one page: edgar (offline) fetches companyfacts for three filers,
' fundamentals.compare builds a peer table of latest-fiscal-year metrics.
' Runs offline against the checked-in fixtures by default; set edgar.identify and
' drop the edgar.offline line to run it live.

function padr(s, w)
    if len(s) >= w then
        return s
    end if
    return s + repeat(" ", w - len(s))
end function

function padl(s, w)
    if len(s) >= w then
        return s
    end if
    return repeat(" ", w - len(s)) + s
end function

' dollars in billions (2dp), or n/a
function bil(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v / 1000000000, 2))
end function

' fraction as a percentage (1dp), or n/a
function pct(v)
    if is_unknown(v) then
        return "n/a"
    end if
    return string(round(v * 100, 1)) + "%"
end function

program main(args)
    load edgar from "../../stdlib/edgar.bas"
    load fundamentals from "../../stdlib/fundamentals.bas"

    cachef(file)= "examples/tmp_edgar_demo.db"
    if exists(cachef) then
        delete(cachef)
    end if

    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_demo.db")
    e = edgar.identify(e, "gBASIC demo tests@example.com")
    e = edgar.offline(e, "examples/fixtures/edgar")

    facts_list = []
    for each ticker in ["AAPL", "JPM", "CROX"]
        append(facts_list, edgar.company_facts(e, edgar.cik(e, ticker)))
    end for

    metrics = ["revenue", "net_income", "fcf", "net_margin"]
    c = fundamentals.compare(facts_list, metrics)

    print("Peer comparison — latest fiscal year (offline fixtures)")
    print(padr("Company", 22) + padl("Revenue$B", 12) + padl("NetInc$B", 11) + padl("FCF$B", 11) + padl("NetMargin", 11))
    i = 0
    while i < count(c["company"])
        line = padr(c["company"][i], 22)
        line = line + padl(bil(c["revenue"][i]), 12)
        line = line + padl(bil(c["net_income"][i]), 11)
        line = line + padl(bil(c["fcf"][i]), 11)
        line = line + padl(pct(c["net_margin"][i]), 11)
        print(line)
        i = i + 1
    end while

    if exists(cachef) then
        delete(cachef)
    end if
end program
