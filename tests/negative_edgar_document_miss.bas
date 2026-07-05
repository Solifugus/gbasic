program main(args)
    load edgar from "../stdlib/edgar.bas"
    cachefile(file)= "examples/tmp_edgar_neg_doc.db"
    if exists(cachefile) then
        delete(cachefile)
    end if
    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_neg_doc.db")
    e = edgar.identify(e, "gBASIC test suite tests@example.com")
    e = edgar.offline(e, "examples/fixtures/edgar")
    ' no document fixture -> offline miss must raise (also exercises URL build)
    print(edgar.document(e, "0001334036", "0001334036-24-000012", "crox-missing.htm"))
end program
