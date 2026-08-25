' WP-EDG-3 — edgar.company_facts + edgar.poll, offline against fixtures.
' company_facts decodes the XBRL record; poll is a pure diff over a submissions
' frame (no fetch), driven here with a chosen last_seen split.
program main(args)
    load edgar from "../stdlib/edgar.bas"

    cachefile{file}= "examples/tmp_edgar_facts.db"
    if exists(cachefile) then
        delete(cachefile)
    end if

    e = edgar.session()
    e = edgar.cache(e, "examples/tmp_edgar_facts.db")
    e = edgar.identify(e, "gBASIC test suite tests@example.com")
    e = edgar.offline(e, "examples/fixtures/edgar")

    cik = edgar.cik(e, "CROX")

    ' company_facts -> decoded record {cik, entityName, facts}
    facts = edgar.company_facts(e, cik)
    print("entity " + facts.entityName)
    print("has_facts " + string(has(facts, "facts")))

    ' poll: submissions is newest-first; take a filing partway down as last_seen.
    subs = edgar.submissions(e, cik)
    total = count(subs.accession)
    print("subs_positive " + string(total > 0))

    seen = subs.accession[5]                 ' the 6th-newest filing
    fresh = edgar.poll(subs, seen)
    print("new_count " + string(count(fresh.accession)))          ' 5 ahead of it
    print("newest_matches " + string(fresh.accession[0] = subs.accession[0]))
    print("excludes_seen " + string(not contains(fresh.accession, seen)))

    ' boundary cases: unknown last_seen -> everything new; newest -> nothing new.
    print("unknown_all_new " + string(count(edgar.poll(subs, unknown).accession) = total))
    print("newest_none_new " + string(count(edgar.poll(subs, subs.accession[0]).accession) = 0))

    if exists(cachefile) then
        delete(cachefile)
    end if
end program
