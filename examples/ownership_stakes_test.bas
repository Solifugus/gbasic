' WP-OWN-4 — ownership.bas: 13D/13G stakes (the fast ownership signal).
' Two REAL structured-era (post-2024 mandate) filings on the SAME subject,
' Trinity Biotech plc (issuer CIK 0000888721):
'   - a SCHEDULE 13G by Novus Diagnostics Ltd. (PASSIVE, classPercent 5.99)
'   - a SCHEDULE 13D/A by Perceptive Advisors LLC (ACTIVIST, percentOfClass 9.9,
'     amendment 8) — a different holder, so not itself a flip.
' The 13G and 13D schemas diverge (issuerCik vs issuerCIK, classPercent vs
' percentOfClass, different reporting-person containers, issuerCusip vs
' issuerCusips/issuerCusipNumber) — stake() reads both, proven by the field-check.
' The passive->activist FLIP (same holder, 13G then 13D) is shown SYNTHETICALLY
' since a real same-filer flip isn't in these two captures.
program main(args)
    load ownership from "../stdlib/ownership.bas"

    g = ownership.stake("examples/fixtures/edgar/sc13g_trinity_novus_sample.xml", "2026-03-06")
    d = ownership.stake("examples/fixtures/edgar/sc13d_trinity_perceptive_sample.xml", "2026-05-04")

    print("== real 13G field-check (Novus / Trinity Biotech) ==")
    print("form=" + g["form"] + " filer=" + g["filer"] + " percent=" + string(g["percent"]) + " amended=" + string(g["amended"]))
    print("issuer_name=" + g["issuer_name"] + " issuer_cik=" + g["issuer_cik"] + " cusip=" + g["cusip"])
    print("event_date=" + g["event_date"] + " filed=" + g["filed"])

    print("== real 13D field-check (Perceptive / Trinity Biotech) ==")
    print("form=" + d["form"] + " filer=" + d["filer"] + " percent=" + string(d["percent"]) + " amended=" + string(d["amended"]))
    print("issuer_name=" + d["issuer_name"] + " issuer_cik=" + d["issuer_cik"] + " cusip=" + d["cusip"])
    print("event_date=" + d["event_date"] + " filed=" + d["filed"])

    ' both real events for the subject stacked into the events frame
    ev = ownership.stakes([g, d])
    print("== events frame (same subject, two holders) ==")
    print("rows=" + string(count(ev["form"])) + " forms=" + ev["form"][0] + "," + ev["form"][1])
    print("filers=" + ev["filer"][0] + " | " + ev["filer"][1])
    print("percents=" + string(ev["percent"][0]) + " | " + string(ev["percent"][1]))

    ' --- synthetic passive->activist FLIP: one holder, 13G then later 13D ---
    g_syn = {
        form: "13G", filer: "Rivendell Capital LP", percent: 6.5, filed: "2026-01-15",
        amended: false, issuer_cik: "0001234567", issuer_name: "Acme Robotics Inc.",
        cusip: "000000AA1", event_date: "01/10/2026"
    }
    d_syn = {
        form: "13D", filer: "Rivendell Capital LP", percent: 8.2, filed: "2026-04-02",
        amended: false, issuer_cik: "0001234567", issuer_name: "Acme Robotics Inc.",
        cusip: "000000AA1", event_date: "03/28/2026"
    }
    flip = ownership.stakes([g_syn, d_syn])
    print("== synthetic flip (same filer) ==")
    i = 0
    while i < count(flip["form"])
        print(flip["filed"][i] + " " + flip["form"][i] + " " + string(flip["percent"][i]) + "% " + flip["filer"][i])
        i = i + 1
    end while
    print("flip visible: 13G->13D ? " + string(flip["form"][0] = "13G" and flip["form"][1] = "13D"))
end program
