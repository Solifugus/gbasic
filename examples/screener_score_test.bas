' WP-SCR-2 — cross-sectional scoring + run() + the concept-map report card, over
' a SYNTHETIC universe of three companyfacts (examples/fixtures/edgar/
' screener_universe/): a HEALTHY filer, a MANIPULATOR (M-Score over threshold),
' and a WEAK filer (negative FCF, sliding into loss). Proves: incremental scoring
' cached in sqlite (resumable via cap), the scored frame, run() selecting the
' quality names, and the unknown long-tail report.

' predicate for run(): a quality screen — strong Piotroski, no manipulation flag,
' positive free cash flow. unknown ingredients exclude (an unscored filer is never
' a hit).
function is_quality(f)
    if is_unknown(f["piotroski"]) then
        return false
    end if
    if is_unknown(f["fcf"]) then
        return false
    end if
    if f["mscore_flag"] = true then
        return false
    end if
    return f["piotroski"] >= 7 and f["fcf"] > 0
end function

program main(args)
    load screener from "../stdlib/screener.bas"
    dir = "examples/fixtures/edgar/screener_universe"

    db{file}= "examples/tmp_screener_scores.db"
    if exists(db) then
        delete(db)
    end if

    ' --- incremental scoring: cap=1 interruptions then finish (resumable) -----
    s1 = screener.score_limited("examples/tmp_screener_scores.db", dir, 1)
    s2 = screener.score_limited("examples/tmp_screener_scores.db", dir, 1)
    s3 = screener.score("examples/tmp_screener_scores.db", dir)
    rescore = screener.score("examples/tmp_screener_scores.db", dir)
    print("== incremental scoring (cap=1 interruptions) ==")
    print("step1=" + string(s1) + " step2=" + string(s2) + " finish=" + string(s3) + " rescore=" + string(rescore))

    ' --- the scored universe frame -------------------------------------------
    u = screener.scored("examples/tmp_screener_scores.db")
    print("== scored universe ==")
    i = 0
    while i < count(u["cik"])
        print(u["cik"][i] + " | " + u["name"][i] + " | F=" + string(u["piotroski"][i]) + " M=" + string(round(u["mscore"][i], 2)) + " flag=" + string(u["mscore_flag"][i]) + " accr=" + string(round(u["accrual_ratio"][i], 3)) + " fcf=" + string(u["fcf"][i]))
        i = i + 1
    end while

    ' --- the market screen ---------------------------------------------------
    hits = screener.run(u, is_quality)
    print("== screen: quality (F>=7, no M-flag, FCF>0) ==")
    j = 0
    while j < count(hits["cik"])
        print("HIT " + hits["cik"][j] + " | " + hits["name"][j])
        j = j + 1
    end while
    print("hits: " + string(count(hits["cik"])) + " of " + string(count(u["cik"])))

    ' --- the concept-map report card (unknown long tail) ---------------------
    rep = screener.unknown_report("examples/tmp_screener_scores.db")
    print("== unknown concept report (" + string(rep["filers"]) + " filers) ==")
    k = 0
    while k < count(rep["concept"])
        if rep["unknown_count"][k] > 0 then
            print(rep["concept"][k] + " : " + string(rep["unknown_count"][k]) + "/" + string(rep["filers"]) + " (" + string(rep["rate"][k]) + ")")
        end if
        k = k + 1
    end while

    if exists(db) then
        delete(db)
    end if
end program
