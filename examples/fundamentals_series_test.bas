' WP-EDG-4 — fundamentals.series concept map + dedup + as_filed, offline.
' 9 series (3 concepts x 3 filers), the latest-accession-wins dedup proof, and
' both unknown paths (unmapped concept; a bank lacking an industrial tag).

' Latest fiscal-year (fp="FY") row of a series, as {end, val}. Hand-checkable
' against the filer's 10-K.
function latest_fy(s)
    best_end = ""
    best_val = unknown
    i = 0
    while i < count(s["end"])
        if s["fp"][i] = "FY" then
            if s["end"][i] > best_end then
                best_end = s["end"][i]
                best_val = s["value"][i]
            end if
        end if
        i = i + 1
    end while
    r = {}
    r["end"] = best_end
    r["val"] = best_val
    return r
end function

function load_facts(path)
    ref(file)= path
    return decode(join(read_lines(ref), "\n"))
end function

program main(args)
    load fundamentals from "../stdlib/fundamentals.bas"

    aapl = load_facts("examples/fixtures/edgar/companyfacts_CIK0000320193.json")
    jpm  = load_facts("examples/fixtures/edgar/companyfacts_CIK0000019617.json")
    crox = load_facts("examples/fixtures/edgar/companyfacts_CIK0001334036.json")

    filers = []
    append(filers, { name: "AAPL", facts: aapl })
    append(filers, { name: "JPM", facts: jpm })
    append(filers, { name: "CROX", facts: crox })

    concepts = ["revenue", "net_income", "total_assets"]

    ' --- 9 series: rows + latest-FY (end, value) ---
    for each concept in concepts
        for each fr in filers
            s = fundamentals.series(fr.facts, concept)
            fy = latest_fy(s)
            print(concept + " " + fr.name + " rows=" + string(count(s["value"])) + " fy_end=" + fy["end"] + " fy_val=" + string(fy["val"]))
        end for
    end for

    ' --- dedup: AAPL net_income FY2007 (end 2007-09-29) filed twice; the later
    ' filing (2010-01-25, accn ...10-012091, val 3495000000) must win. ---
    ni = fundamentals.series(aapl, "net_income")
    nia = fundamentals.series_as_filed(aapl, "net_income")
    print("dedup rows_deduped=" + string(count(ni["value"])) + " rows_as_filed=" + string(count(nia["value"])))
    i = 0
    hit = unknown
    while i < count(ni["end"])
        if ni["end"][i] = "2007-09-29" then
            if ni["fp"][i] = "FY" then
                hit = i
            end if
        end if
        i = i + 1
    end while
    print("dedup fy2007_val=" + string(ni["value"][hit]) + " accn=" + ni["accession"][hit])

    ' --- unknown paths ---
    print("jpm operating_income unknown=" + string(is_unknown(fundamentals.series(jpm, "operating_income"))))
    print("unmapped concept unknown=" + string(is_unknown(fundamentals.series(aapl, "not_a_real_concept"))))
end program
