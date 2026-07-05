program main(args)
    load edgar from "../stdlib/edgar.bas"
    cachefile(file)= "examples/tmp_edgar_neg_identity.db"
    if exists(cachefile) then
        delete(cachefile)
    end if
    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_neg_identity.db")
    e = edgar.offline(e, "examples/fixtures/edgar")
    ' no edgar.identify -> the first fetch must raise
    print(edgar.cik(e, "AAPL"))
end program
