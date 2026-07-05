' WP-EDG-2 — edgar.bas acquisition core, exercised entirely offline against the
' checked-in fixtures (examples/fixtures/edgar). No network. Proves: identity +
' offline seam + sqlite cache + the transport counter (cache-hit assertion).
program main(args)
    load edgar from "../stdlib/edgar.bas"

    ' Fresh cache each run so counts are deterministic.
    cachefile(file)= "examples/tmp_edgar_test.db"
    if exists(cachefile) then
        delete(cachefile)
    end if

    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_test.db")
    e = edgar.identify(e, "gBASIC test suite tests@example.com")
    e = edgar.offline(e, "examples/fixtures/edgar")

    ' ticker -> CIK (10-digit zero-padded strings; missing ticker -> unknown)
    print("aapl " + edgar.cik(e, "AAPL"))
    print("jpm " + edgar.cik(e, "JPM"))
    print("crox " + edgar.cik(e, "crox"))
    print("bogus_unknown " + string(is_unknown(edgar.cik(e, "ZZZZ"))))

    ' submissions -> filing-index frame (record of equal-length column lists)
    subs = edgar.submissions(e, edgar.cik(e, "CROX"))
    print("cols " + join(keys(subs), ","))
    print("rows_positive " + string(count(subs.form) > 0))
    print("accession_is_string " + string(is_string(subs.accession[0])))

    ' transport counter: the tickers fetch + the submissions fetch = 2 so far.
    n1 = edgar.transport_count(e)
    print("transport_after_fetches " + string(n1))

    ' second identical submissions call is a cache hit -> no new transport.
    subs2 = edgar.submissions(e, "0001334036")
    n2 = edgar.transport_count(e)
    print("transport_after_cachehit " + string(n2))
    print("cachehit_no_transport " + string(n2 = n1))

    if exists(cachefile) then
        delete(cachefile)
    end if
end program
