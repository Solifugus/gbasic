' WP-FOR-2 — forensics.beneish, goldened against the APPROVED published worked
' example (Boeing FY2023, StableBread) whose raw two-year statement inputs are in
' examples/fixtures/edgar/beneish_boeing_fixture.json. Also: a composite-only
' cross-check against WallStreetMojo's published indices, and unknown-propagation
' when an ingredient is missing.
program main(args)
    load forensics from "../stdlib/forensics.bas"

    ' --- primary golden: raw statements -> 8 indices -> M-Score (Boeing FY2023) ---
    p{file}= "examples/fixtures/edgar/beneish_boeing_fixture.json"
    facts = decode(join(read_lines(p), "\n"))
    b = forensics.beneish(facts)
    print("== Boeing FY2023 (published reference) ==")
    print("period " + b["prior_end"][0] + " -> " + b["end"][0])
    print("DSRI=" + string(round(b["dsri"][0], 3)) + " GMI=" + string(round(b["gmi"][0], 3)) + " AQI=" + string(round(b["aqi"][0], 3)) + " SGI=" + string(round(b["sgi"][0], 3)))
    print("DEPI=" + string(round(b["depi"][0], 3)) + " SGAI=" + string(round(b["sgai"][0], 3)) + " LVGI=" + string(round(b["lvgi"][0], 3)) + " TATA=" + string(round(b["tata"][0], 3)))
    print("MSCORE=" + string(round(b["mscore"][0], 2)) + " flag(manipulator)=" + string(b["flag"][0]))

    ' --- composite cross-check: WallStreetMojo published indices -> M = -2.53 ---
    wsm = { dsri: 0.814, gmi: 1.556, aqi: 0.608, sgi: 0.755, depi: 0.801, sgai: 1.110, lvgi: 0.878, tata: 0.044 }
    print("== WallStreetMojo composite cross-check ==")
    print("MSCORE=" + string(round(forensics.mscore(wsm), 2)))

    ' --- unknown propagation: drop net PP&E -> AQI & DEPI unknown -> M unknown ---
    facts2 = decode(join(read_lines(p), "\n"))
    facts2.facts["us-gaap"] = remove_key(facts2.facts["us-gaap"], "PropertyPlantAndEquipmentNet")
    b2 = forensics.beneish(facts2)
    print("== unknown propagation (no PP&E) ==")
    print("DSRI known? " + string(not is_unknown(b2["dsri"][0])) + " (PPE-free index still computes)")
    print("AQI unknown? " + string(is_unknown(b2["aqi"][0])) + " DEPI unknown? " + string(is_unknown(b2["depi"][0])))
    print("MSCORE unknown? " + string(is_unknown(b2["mscore"][0])) + " flag unknown? " + string(is_unknown(b2["flag"][0])))
    print("mscore(one unknown index) unknown? " + string(is_unknown(forensics.mscore({ dsri: 1, gmi: 1, aqi: unknown, sgi: 1, depi: 1, sgai: 1, lvgi: 1, tata: 1 }))))
end program
