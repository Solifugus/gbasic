' WP-MDA-2 — mdna deterministic pre-pass: risk-factor YoY diff, hedge density,
' and evidence assembly. Real diff/hedge goldens over the two consecutive Crocs
' 10-Ks (FY2024 -> FY2025), a synthetic risk pair with KNOWN adds/removes, and an
' evidence() record carrying a supplied forensics scorecard.
program main(args)
    load mdna from "../stdlib/mdna.bas"

    p2024{file}= "examples/fixtures/edgar/tenk_crox_2024_sample.htm"
    p2025{file}= "examples/fixtures/edgar/tenk_crox_2025_sample.htm"
    prior = mdna.sections(join(read_lines(p2024), "\n"))
    curr = mdna.sections(join(read_lines(p2025), "\n"))

    ' --- real risk-factor diff (counts) ---
    rd = mdna.risk_diff(prior["risk_factors"], curr["risk_factors"])
    print("== real risk_diff FY2024->FY2025 ==")
    print("prior_sentences=" + string(rd.prior_count) + " current_sentences=" + string(rd.current_count))
    print("added=" + string(rd.added_count) + " removed=" + string(rd.removed_count) + " common=" + string(rd.common_count))

    ' --- real hedge density (per 10k words of the MD&A) ---
    hp = mdna.hedge_stats(prior["mdna"])
    hc = mdna.hedge_stats(curr["mdna"])
    print("== real hedge density (MD&A) ==")
    print("FY2024 hedge=" + string(hp.hedge) + "/" + string(hp.total) + " per10k=" + string(round(hp.rate * 10000, 0)))
    print("FY2025 hedge=" + string(hc.hedge) + "/" + string(hc.total) + " per10k=" + string(round(hc.rate * 10000, 0)))
    print("shift per10k=" + string(round((hc.rate - hp.rate) * 10000, 0)))

    ' --- synthetic pair with KNOWN adds/removes ---
    pr = "We depend on key suppliers for materials. Currency fluctuations may hurt our reported results. Our brand remains concentrated in clogs footwear. Cybersecurity incidents could disrupt operations."
    cu = "We depend on key suppliers for materials. Currency fluctuations may hurt our reported results. New import tariffs may materially increase our costs. Cybersecurity incidents could disrupt operations. Ongoing litigation risk has increased this year."
    sd = mdna.risk_diff(pr, cu)
    print("== synthetic risk_diff ==")
    print("added=" + string(sd.added_count) + " removed=" + string(sd.removed_count) + " common=" + string(sd.common_count))
    for each a in sd.added
        print("ADDED: " + a)
    end for
    for each r in sd.removed
        print("REMOVED: " + r)
    end for

    ' --- hedge rate on a hedgy vs plain sentence ---
    print("== hedge_rate spot check ==")
    print("hedgy per10k=" + string(round(mdna.hedge_rate("We believe results may possibly improve and could likely expand.") * 10000, 0)))
    print("plain per10k=" + string(round(mdna.hedge_rate("Revenue was four billion dollars for the year ended.") * 10000, 0)))

    ' --- evidence assembly with a SUPPLIED forensics scorecard ---
    scorecard = { accrual_ratio: 0.08, m_score: -1.78, red_flags: ["rising DSO", "negative FCF"] }
    ev = mdna.evidence(prior, curr, scorecard)
    print("== evidence record ==")
    print("risk_added=" + string(ev["risk_added_count"]) + " risk_removed=" + string(ev["risk_removed_count"]))
    print("hedge_shift per10k=" + string(round(ev["hedge_shift"] * 10000, 0)))
    print("scorecard carried: m_score=" + string(ev["scorecard"].m_score) + " red_flags=" + string(count(ev["scorecard"].red_flags)))
end program
