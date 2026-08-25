' WP-SCR-1 — screener bulk ingest, entirely offline over a truncated companyfacts
' sample (examples/fixtures/edgar/companyfacts_sample/CIK{10}.json for AAPL, JPM,
' CROX; built by tools/screener_sample_build.sh). Proves: ingest-twice row count ==
' ingest-once (idempotent), resumability under a cap-injection interruption (a
' re-run continues and never duplicates), and the universe frame.
program main(args)
    load screener from "../stdlib/screener.bas"
    dir = "examples/fixtures/edgar/companyfacts_sample"

    ' --- idempotency: ingest twice == ingest once ---
    dbA{file}= "examples/tmp_screener_a.db"
    if exists(dbA) then
        delete(dbA)
    end if
    once = screener.ingest("examples/tmp_screener_a.db", dir)
    rows_once = count(screener.universe("examples/tmp_screener_a.db")["cik"])
    again = screener.ingest("examples/tmp_screener_a.db", dir)
    rows_twice = count(screener.universe("examples/tmp_screener_a.db")["cik"])
    print("== idempotency ==")
    print("ingest-once  processed=" + string(once) + " rows=" + string(rows_once))
    print("ingest-again processed=" + string(again) + " rows=" + string(rows_twice))
    print("twice == once ? " + string(rows_twice = rows_once))

    ' --- resumability: simulate interruption with a cap of 1 new filer per run ---
    dbB{file}= "examples/tmp_screener_b.db"
    if exists(dbB) then
        delete(dbB)
    end if
    step1 = screener.ingest_limited("examples/tmp_screener_b.db", dir, 1)
    after1 = count(screener.universe("examples/tmp_screener_b.db")["cik"])
    step2 = screener.ingest_limited("examples/tmp_screener_b.db", dir, 1)
    after2 = count(screener.universe("examples/tmp_screener_b.db")["cik"])
    step3 = screener.ingest("examples/tmp_screener_b.db", dir)
    after3 = count(screener.universe("examples/tmp_screener_b.db")["cik"])
    print("== resumability (cap=1 interruptions) ==")
    print("cap#1: processed=" + string(step1) + " rows=" + string(after1))
    print("cap#2: processed=" + string(step2) + " rows=" + string(after2))
    print("finish: processed=" + string(step3) + " rows=" + string(after3))
    print("no duplication (rows == once) ? " + string(after3 = rows_once))

    ' --- universe golden ---
    print("== universe ==")
    u = screener.universe("examples/tmp_screener_b.db")
    i = 0
    while i < count(u["cik"])
        print(u["cik"][i] + " | " + u["name"][i] + " | " + u["latest_period"][i])
        i = i + 1
    end while

    if exists(dbA) then
        delete(dbA)
    end if
    if exists(dbB) then
        delete(dbB)
    end if
end program
