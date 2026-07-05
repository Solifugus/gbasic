' WP-OWN-3 — ownership.bas: 13F report frame + quarter delta. Uses two REAL
' consecutive quarters of ONE filer (CIK 1596355): Q1 2026 (period 2026-03-31,
' filed 2026-04-08, 19 holdings) and Q2 2026 (period 2026-06-30, filed 2026-07-02,
' 17 holdings). report_13f streams each info table via skip_to/subtree. The real
' pair happens to have NO new position (2 exited, rest changed), so a SYNTHETIC
' hand-computed pair demonstrates one new / one exited / one changed explicitly.
' A pre-2023 filing date exercises the thousands->dollars value normalization.
program main(args)
    load ownership from "../stdlib/ownership.bas"

    q1 = ownership.report_13f("examples/fixtures/edgar/f13_infotable_2026q1_sample.xml", "2026-04-08")
    q2 = ownership.report_13f("examples/fixtures/edgar/f13_infotable_sample.xml", "2026-07-02")

    print("== report field-check ==")
    print("q1 count=" + string(count(q1["cusip"])) + " total_value=" + string(sum(q1["value"])))
    print("q1 row0: " + q1["issuer"][0] + " | " + q1["cusip"][0] + " | " + string(q1["value"][0]) + " | " + string(q1["shares"][0]))
    print("q2 count=" + string(count(q2["cusip"])) + " total_value=" + string(sum(q2["value"])))
    print("q2 row0: " + q2["issuer"][0] + " | " + q2["cusip"][0] + " | " + string(q2["value"][0]) + " | " + string(q2["shares"][0]))

    ' --- value-unit normalization (thousands historically) ---
    q2_old = ownership.report_13f("examples/fixtures/edgar/f13_infotable_sample.xml", "2019-02-14")
    print("== normalization ==")
    print("2019-filed value0 == 2026 value0 * 1000 ? " + string(q2_old["value"][0] = q2["value"][0] * 1000))

    ' --- real delta q1 -> q2 (exited + changed; no new in this pair) ---
    rd = ownership.delta(q1, q2)
    print("== real delta q1->q2 ==")
    ne = 0
    nx = 0
    nc = 0
    nu = 0
    i = 0
    while i < count(rd["status"])
        s = rd["status"][i]
        if s = "new" then
            ne = ne + 1
        end if
        if s = "exited" then
            nx = nx + 1
        end if
        if s = "changed" then
            nc = nc + 1
        end if
        if s = "unchanged" then
            nu = nu + 1
        end if
        i = i + 1
    end while
    print("counts new=" + string(ne) + " exited=" + string(nx) + " changed=" + string(nc) + " unchanged=" + string(nu))
    ' exited names (hand-read: Exxon Mobil 30231G102, Millrose 601137102)
    i = 0
    while i < count(rd["status"])
        if rd["status"][i] = "exited" then
            print("exited: " + rd["issuer"][i] + " | " + rd["cusip"][i] + " | prior_shares=" + string(rd["prior_shares"][i]) + " delta_shares=" + string(rd["delta_shares"][i]))
        end if
        i = i + 1
    end while
    ' one changed spot-check: Accenture G1151C101 (26380 -> 26295, dsh -85)
    j = ownership_index(rd["cusip"], "G1151C101")
    print("changed Accenture: prior_shares=" + string(rd["prior_shares"][j]) + " shares=" + string(rd["shares"][j]) + " delta_shares=" + string(rd["delta_shares"][j]) + " delta_value=" + string(rd["delta_value"][j]))

    ' --- synthetic delta: one new, one exited, one changed, one unchanged ---
    prior = {
        cusip:  ["AAA",   "BBB",  "CCC"],
        issuer: ["Alpha", "Beta", "Gamma"],
        value:  [1000,    2000,   3000],
        shares: [100,     200,    300]
    }
    current = {
        cusip:  ["AAA",   "CCC",   "DDD"],
        issuer: ["Alpha", "Gamma", "Delta"],
        value:  [1500,    3000,    4000],
        shares: [150,     300,     400]
    }
    sd = ownership.delta(prior, current)
    print("== synthetic delta ==")
    k = 0
    while k < count(sd["cusip"])
        print(sd["cusip"][k] + " " + sd["status"][k] + " prior_sh=" + string(sd["prior_shares"][k]) + " sh=" + string(sd["shares"][k]) + " dsh=" + string(sd["delta_shares"][k]) + " dval=" + string(sd["delta_value"][k]))
        k = k + 1
    end while
end program

' local index-of helper (the library's is private)
function ownership_index(arr, x)
    i = 0
    while i < count(arr)
        if arr[i] = x then
            return i
        end if
        i = i + 1
    end while
    return -1
end function
