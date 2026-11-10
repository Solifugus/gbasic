' WP-XML-2 — xml.find / find_all / text / attr over a REAL Form 4 (insider
' transaction) XML fixture, plus mini-path wildcard, attribute reading, and the
' §8 absence semantics (unknown, and xml.text of unknown raising since
' 2026-11-09, so a field a transaction may lack is checked first).
program main(args)
    load xml
    doc = xml.parse_file("examples/fixtures/edgar/form4_sample.xml")

    ' --- real Form 4 field-check (hand-checkable against the document) ---
    print("doc_type=" + xml.text(xml.find(doc, "documentType")))
    print("symbol=" + xml.text(xml.find(doc, "issuer/issuerTradingSymbol")))
    print("owner_cik=" + xml.text(xml.find(doc, "reportingOwner/reportingOwnerId/rptOwnerCik")))
    print("owner_name=" + xml.text(xml.find(doc, "reportingOwner/reportingOwnerId/rptOwnerName")))

    txns = xml.find_all(doc, "nonDerivativeTable/nonDerivativeTransaction")
    print("nonderiv_txn_count=" + string(count(txns)))
    i = 0
    while i < count(txns)
        t = txns[i]
        sec = xml.text(xml.find(t, "securityTitle/value"))
        code = xml.text(xml.find(t, "transactionCoding/transactionCode"))
        shares = xml.text(xml.find(t, "transactionAmounts/transactionShares/value"))
        ' a price is absent from an option exercise (txn0), so the miss is decided here
        pn = xml.find(t, "transactionAmounts/transactionPricePerShare/value")
        price = ""
        if not is_unknown(pn) then
            price = xml.text(pn)
        end if
        print("  txn" + string(i) + " security=" + sec + " code=" + code + " shares=" + shares + " price=[" + price + "]")
        i = i + 1
    end while

    ' --- mini-path wildcard: * matches any child element ---
    print("wildcard_symbol=" + xml.text(xml.find(doc, "issuer/*")))

    ' --- attributes (a hand-built shape exercises xml.attr) ---
    a = xml.parse("<rec id='42' kind='sale'><n>x</n></rec>")
    print("attr_id=" + xml.attr(a, "id") + " attr_kind=" + xml.attr(a, "kind"))
    print("attr_missing_unknown=" + string(is_unknown(xml.attr(a, "absent"))))
    print("attr_default=" + xml.attr(a, "absent", "none"))

    ' --- §8 absence semantics: find miss -> unknown, text(unknown) raises ---
    print("find_miss_unknown=" + string(is_unknown(xml.find(doc, "no/such/path"))))
    print("find_all_miss_empty=" + string(count(xml.find_all(doc, "no/such/path"))))
    on error goto next
    s = xml.text(xml.find(doc, "nope"))
    print("text_of_unknown_raises=" + string(contains(error.message, "xml.text was given unknown")))
    error.clear()
end program
