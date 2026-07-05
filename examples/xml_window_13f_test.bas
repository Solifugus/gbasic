' WP-XML-5 — skip_to + subtree windowing over a REAL 13F information table
' (examples/fixtures/edgar/f13_infotable_sample.xml — CIK 1596355, accession
' 0001596355-26-000003, captured live; 17 holdings, default-namespaced so the
' local-name match in skip_to/find is exercised). This is xml_design.md §4's
' windowing loop verbatim: stream at the file level, one <infoTable> at a time
' materialized into an ordinary record tree via subtree, memory bounded by the
' holding, not the document. Spot-check rows are hand-read from the fixture.
program main(args)
    load xml

    r = xml.reader("examples/fixtures/edgar/f13_infotable_sample.xml")
    rows = []
    total = 0
    while xml.skip_to(r, "infoTable")
        t = xml.subtree(r)
        issuer = xml.text(xml.find(t, "nameOfIssuer"))
        cusip = xml.text(xml.find(t, "cusip"))
        value = number(xml.text(xml.find(t, "value")))
        shares = number(xml.text(xml.find(t, "shrsOrPrnAmt/sshPrnamt")))
        append(rows, { issuer: issuer, cusip: cusip, value: value, shares: shares })
        total = total + value
    end while
    xml.close(r)

    print("count=" + string(count(rows)))
    print("total_value=" + string(total))
    print("row1: " + rows[0]["issuer"] + " | " + rows[0]["cusip"] + " | " + string(rows[0]["value"]) + " | " + string(rows[0]["shares"]))
    print("row5: " + rows[4]["issuer"] + " | " + rows[4]["cusip"] + " | " + string(rows[4]["value"]) + " | " + string(rows[4]["shares"]))
    print("row17: " + rows[16]["issuer"] + " | " + rows[16]["cusip"] + " | " + string(rows[16]["value"]) + " | " + string(rows[16]["shares"]))
end program
