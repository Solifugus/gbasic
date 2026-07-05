program main(args)
    load edgar from "../stdlib/edgar.bas"
    cachefile(file)= "examples/tmp_edgar_neg_miss.db"
    if exists(cachefile) then
        delete(cachefile)
    end if
    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_neg_miss.db")
    e = edgar.identify(e, "gBASIC test suite tests@example.com")
    e = edgar.offline(e, "examples/fixtures/edgar")
    ' no fixture for this CIK -> offline miss must raise
    print(count(edgar.submissions(e, "9999999999").form))
end program
