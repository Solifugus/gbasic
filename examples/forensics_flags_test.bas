' WP-FOR-4 — forensics.flags: the composite red-flag dossier. Two runs:
'   (1) a SYNTHETIC pair of fixtures engineered to exercise every flag kind:
'       * flags_synthetic_subs.json  — a submissions frame with NT / 10-K/A /
'         8-K 4.01 / 8-K 4.02 / clustered 8-K 5.02 filings (plus benign filings
'         and one ISOLATED 5.02 that must NOT cluster),
'       * flags_synthetic_facts.json — 3 fiscal years whose 2023 pair fires
'         rising accruals, an M-Score over -1.78, and positive-NI-negative-FCF;
'       and the window parameter: a 10-day window dissolves the 5.02 cluster.
'   (2) a REAL filer (Apple) over its REAL companyfacts + submissions — runs
'       without error and yields a possibly-empty dossier; counts recorded.
program main(args)
    load forensics from "../stdlib/forensics.bas"

    pf{file}= "examples/fixtures/edgar/flags_synthetic_facts.json"
    ps{file}= "examples/fixtures/edgar/flags_synthetic_subs.json"
    facts = decode(join(read_lines(pf), "\n"))
    subs = decode(join(read_lines(ps), "\n"))

    f = forensics.flags(facts, subs)
    print("== synthetic red-flag dossier (default 90-day cluster window) ==")
    i = 0
    while i < count(f["kind"])
        acc = f["accession"][i]
        if is_unknown(acc) then
            acc = "-"
        end if
        print(f["kind"][i] + " | " + f["date"][i] + " | acc=" + acc + " | period=" + f["period"][i] + " | " + f["detail"][i])
        i = i + 1
    end while
    print("rows: " + string(count(f["kind"])))

    ' --- window parameter: 10 days is too tight to cluster the May 5.02s ------
    f10 = forensics.flags_window(facts, subs, 10)
    exodus = 0
    for each k in f10["kind"]
        if k = "officer_exodus" then
            exodus = exodus + 1
        end if
    end for
    print("== 10-day window ==")
    print("officer_exodus rows: " + string(exodus))

    ' --- real filer: Apple, real companyfacts + real submissions -------------
    pff{file}= "examples/fixtures/edgar/companyfacts_CIK0000320193.json"
    psf{file}= "examples/fixtures/edgar/submissions_CIK0000320193.json"
    aapl = decode(join(read_lines(pff), "\n"))
    raw = decode(join(read_lines(psf), "\n"))
    r = raw.filings.recent
    asubs = {}
    asubs["form"] = r.form
    asubs["filed"] = r.filingDate
    asubs["accession"] = r.accessionNumber
    asubs["period"] = r.reportDate
    asubs["items"] = r.items

    fa = forensics.flags(aapl, asubs)
    print("== real filer (AAPL) over real submissions ==")
    print("submissions rows scanned: " + string(count(asubs["form"])))
    print("total flag rows: " + string(count(fa["kind"])))
    kinds = ["nt_filing", "amendment", "auditor_change", "non_reliance", "officer_exodus", "rising_accruals", "mscore_flag", "positive_ni_negative_fcf"]
    for each kind in kinds
        c = 0
        for each k in fa["kind"]
            if k = kind then
                c = c + 1
            end if
        end for
        print("  " + kind + ": " + string(c))
    end for
end program
