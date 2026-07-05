' WP-OWN-2 — insiders.bas convenience compositions: conviction + cluster.
' Real Form 4 fixtures rarely contain multi-insider clusters, so this drives
' SYNTHETIC code-P buys frames (the shape insiders.open_market_buys returns).
' conviction = value / prior_stake, prior_stake = post_shares - shares; a
' zero prior stake (first-ever buy, post==shares) or an unknown ingredient
' yields unknown (no divide-by-zero). cluster groups buys within `window` days
' of the cluster's first buy and reports distinct owners (>=2 = the signal).
program main(args)
    load insiders from "../stdlib/insiders.bas"

    ' Synthetic buys: dates ascending. Bob's post==shares (first-ever buy -> zero
    ' prior stake). Dave's price/value are unknown (footnoted). Two Alice buys and
    ' one Dave buy sit >7 days after the opening Alice+Bob pair.
    buys = {
        code:        ["P",          "P",          "P",          "P",          "P"],
        owner:       ["Alice",      "Bob",        "Alice",      "Alice",      "Dave"],
        shares:      [1000,         500,          2000,         300,          100],
        price:       [10,           20,           10,           5,            unknown],
        value:       [10000,        10000,        20000,        1500,         unknown],
        post_shares: [5000,         500,          8000,         1300,         600],
        date:        ["2026-05-01", "2026-05-03", "2026-05-20", "2026-05-21", "2026-06-15"]
    }

    ' --- conviction ---
    conv = insiders.conviction(buys)
    print("== conviction ==")
    i = 0
    while i < count(conv["owner"])
        print(conv["owner"][i] + " value=" + string(conv["value"][i]) + " prior_stake=" + string(conv["prior_stake"][i]) + " conviction=" + string(conv["conviction"][i]))
        i = i + 1
    end while
    print("alice0 conviction == 10000/4000 ? " + string(conv["conviction"][0] = 10000 / 4000))
    print("bob zero-prior conviction is_unknown ? " + string(is_unknown(conv["conviction"][1])))
    print("dave unknown-value conviction is_unknown ? " + string(is_unknown(conv["conviction"][4])))

    ' --- cluster (window = 7 days) ---
    cl = insiders.cluster(buys, 7)
    print("== cluster (window=7) ==")
    print("num_clusters=" + string(count(cl["start"])))
    c = 0
    while c < count(cl["start"])
        print("cluster " + cl["start"][c] + ".." + cl["end"][c] + " owners=" + string(cl["owners"][c]) + " buys=" + string(cl["buys"][c]) + " value=" + string(cl["value"][c]))
        c = c + 1
    end while
end program
