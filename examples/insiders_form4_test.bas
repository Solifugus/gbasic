' WP-OWN-1 — insiders.bas: Form 4 -> transaction frame + code-P screen.
' Part 1 field-checks the REAL fixture (examples/fixtures/edgar/form4_sample.xml,
' Apple Form 4 for Jennifer Newstead): two non-derivative transactions, an
' M/A option-settlement (price footnoted -> unknown) and an F/D tax-withholding
' at 296.42. Part 2 uses a SYNTHETIC Form 4 carrying a code-P open-market buy
' (the real fixture has none) to exercise the screen positively, and concat to
' show multiple Form 4s per filer combine. Prices/values that are footnoted are
' `unknown`, never guessed.
program main(args)
    load insiders from "../stdlib/insiders.bas"
    load xml

    ' --- Part 1: real fixture ------------------------------------------------
    p{file}= "examples/fixtures/edgar/form4_sample.xml"
    doc = xml.parse(join(read_lines(p), "\n"))
    tx = insiders.from_form4(doc, "2026-06-17")

    print("== real fixture ==")
    print("nrows=" + string(count(tx["code"])))
    print("codes=" + join(tx["code"], ","))
    print("acquired=" + join(tx["acquired"], ","))
    print("owner=" + tx["owner"][0])
    print("officer_title=" + tx["officer_title"][0])
    print("is_officer=" + string(tx["is_officer"][0]) + " is_director=" + string(tx["is_director"][0]))
    print("row0 M/A: shares=" + string(tx["shares"][0]) + " price=" + string(tx["price"][0]) + " value=" + string(tx["value"][0]) + " post_shares=" + string(tx["post_shares"][0]) + " date=" + tx["date"][0] + " filed=" + tx["filed"][0])
    print("row1 F/D: shares=" + string(tx["shares"][1]) + " price=" + string(tx["price"][1]) + " post_shares=" + string(tx["post_shares"][1]) + " date=" + tx["date"][1])
    print("row1 value == shares*price ? " + string(tx["value"][1] = 16238 * 296.42))
    print("row0 price is_unknown ? " + string(is_unknown(tx["price"][0])))

    ' code-P screen over the real fixture: no P present -> empty, columns kept
    buys0 = insiders.open_market_buys(tx)
    print("open_market_buys(real) nrows=" + string(count(buys0["code"])))

    ' --- Part 2: synthetic code-P Form 4 -------------------------------------
    soup = "<ownershipDocument>"
    soup = soup + "<reportingOwner><reportingOwnerId><rptOwnerCik>0000000001</rptOwnerCik><rptOwnerName>Buyer Jane</rptOwnerName></reportingOwnerId>"
    soup = soup + "<reportingOwnerRelationship><isDirector>true</isDirector></reportingOwnerRelationship></reportingOwner>"
    soup = soup + "<nonDerivativeTable><nonDerivativeTransaction>"
    soup = soup + "<transactionDate><value>2026-05-10</value></transactionDate>"
    soup = soup + "<transactionCoding><transactionCode>P</transactionCode></transactionCoding>"
    soup = soup + "<transactionAmounts><transactionShares><value>1000</value></transactionShares>"
    soup = soup + "<transactionPricePerShare><value>150.5</value></transactionPricePerShare>"
    soup = soup + "<transactionAcquiredDisposedCode><value>A</value></transactionAcquiredDisposedCode></transactionAmounts>"
    soup = soup + "<postTransactionAmounts><sharesOwnedFollowingTransaction><value>5000</value></sharesOwnedFollowingTransaction></postTransactionAmounts>"
    soup = soup + "</nonDerivativeTransaction></nonDerivativeTable></ownershipDocument>"
    sdoc = xml.parse(soup)
    stx = insiders.from_form4(sdoc, "2026-05-12")

    print("== synthetic P ==")
    print("owner=" + stx["owner"][0] + " is_director=" + string(stx["is_director"][0]) + " is_officer=" + string(stx["is_officer"][0]))
    print("code=" + stx["code"][0] + " acquired=" + stx["acquired"][0] + " shares=" + string(stx["shares"][0]) + " price=" + string(stx["price"][0]) + " value=" + string(stx["value"][0]))

    ' --- Part 3: concat (multiple Form 4s) + positive code-P screen ----------
    both = insiders.concat(tx, stx)
    print("== concat + screen ==")
    print("concat nrows=" + string(count(both["code"])))
    buys = insiders.open_market_buys(both)
    print("buys nrows=" + string(count(buys["code"])))
    print("buy owner=" + buys["owner"][0] + " code=" + buys["code"][0] + " value=" + string(buys["value"][0]) + " date=" + buys["date"][0])
end program
