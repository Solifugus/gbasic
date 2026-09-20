' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' ari_discover Phase 4 -- the optional LLM advisor.
'
' SELF-CHECKING, and here that is forced: every defect this layer can have is a
' PLAUSIBLE NAME. `transaction_amount` instead of `amount` looks like an
' improvement whether or not anything justified it, and a golden would record
' whichever name came back and defend it. So each check states the answer it
' wants and prints a MISMATCH naming both sides.
'
' NO NETWORK AND NO KEY. Every model call replays a recorded response
' (tests/ari_advisor/record.bas made it against live OpenAI). A gate needing
' credentials goes quiet the day a key expires.
load ari_discover
load ari_advisor
load llm

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.bad = G.bad + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
end function

program main( args )
    G = { checks: 0, bad: 0 }

    srcs = []
    i = 1
    while i <= 8
        f {file}= "examples/fixtures/ari_discover/0" + string(i) + "_branch_activity.rpt"
        append(srcs, { id: string(i), text: read(f) })
        i = i + 1
    end while
    p = ari_discover.infer(srcs)
    check("the deterministic proposal stands on its own", p.ok, true)

    print ""
    print "-- EVIDENCE: what leaves the machine --"
    ' THE ADVISOR NEVER SEES THE REPORT. The package is built from the PROPOSAL,
    ' so a value that was never concluded cannot travel.
    pkg = ari_advisor.evidence(p)
    check("the package names its mode", pkg.mode, "redacted")
    check("and carries the row SIGNATURE, not the family record", pkg.row_signature,
          "<IDENTIFIER> <TEXT> <TEXT> <DATE> <MONEY>")
    ' THE FAMILY RECORD CARRIES 45 LINE NUMBERS. Sending it whole would make the
    ' package grow with the REPORT rather than with its STRUCTURE, which is the
    ' opposite of the minimization §11.1 asks for -- so this is asserted as an
    ' ABSENCE, the only way to catch a package that quietly got bigger.
    check("and does not carry the line numbers", has(pkg, "lines"), false)
    check("and does not carry the report text", has(pkg, "text"), false)

    print ""
    print "-- MASKING: a shape, not a value --"
    ' STRUCTURAL, NOT REMOVAL: "[redacted]" would be private and useless, since
    ' the shape IS the evidence a name is inferred from.
    check("digits become 9", ari_advisor.mask("1,250.00"), "9,999.99")
    check("letters keep their case", ari_advisor.mask("Smith, J"), "Xxxxx, X")
    check("and punctuation survives", ari_advisor.mask("2026-09-20"), "9999-99-99")
    ' THE CONTROL. Masking that changed nothing would pass every check above if
    ' they were only "the example is present".
    check("an example really is masked in the package", pkg.fields[0].example, "99-XXX-9999")

    print ""
    print "-- NAME ADVICE: proposed, never adopted --"
    m = llm.replay(llm.openai("gpt-4o-mini", "not-used-in-replay"), "tests/ari_advisor/replay")
    r = ari_advisor.advise_names(p, m)
    check("the advisor answered", r.advisor.answered, true)
    check("and said which prompt version", r.advisor.prompt_version, "ari-advisor-1")

    ' THE LOAD-BEARING CHECK. A model's suggestion must NOT become the field's
    ' name, because nothing can score it: `posted` and `posted_date` parse the
    ' corpus identically, with identical coverage and identical collisions. The
    ' suggestion is beside the name, not in it.
    f0 = r.fields[0]
    check("the field keeps its deterministic name", f0.name, "posted")
    check("the suggestion sits beside it", f0.advice.proposed, "posted_date")
    check("and is marked NOT adopted", f0.advice.adopted, false)
    check("and says who proposed it", f0.advice.source, "llm")

    print ""
    print "-- ADOPTION IS A PERSON'S ACT --"
    a = ari_advisor.adopt(r, "posted")
    check("adopting changes the name", a.fields[0].name, "posted_date")
    check("and keeps what it was", a.fields[0].name_was, "posted")
    check("and records that it was adopted", a.fields[0].advice.adopted, true)
    ' THE OTHER FIELD IS UNTOUCHED -- adopt takes ONE name, and there is no
    ' adopt_all, because accepting every suggestion in one call is the silent
    ' adoption this library exists to prevent wearing a person's clothes.
    check("the other field is untouched", a.fields[1].name, "amount")
    check("and still marked not adopted", a.fields[1].advice.adopted, false)

    print ""
    print "-- REFUSALS --"
    on error goto next
    bad = ari_advisor.evidence(p, { mode: "sideways" })
    if error then
        check("an unknown mode is refused", contains(error.message, "unknown mode 'sideways'"), true)
        error.clear()
    else
        check("an unknown mode is refused", "accepted", "refused")
    end if

    on error goto next
    bad2 = ari_advisor.evidence(p, { redact: true })
    if error then
        check("an unknown option is refused BY NAME", contains(error.message, "unknown option 'redact'"), true)
        error.clear()
    else
        check("an unknown option is refused BY NAME", "accepted", "refused")
    end if

    on error goto next
    bad3 = ari_advisor.advise_names(p, m, { mode: "off" })
    if error then
        check("mode off refuses to call the advisor", contains(error.message, "mode is off"), true)
        error.clear()
    else
        check("mode off refuses to call the advisor", "called", "refused")
    end if

    ' THE REFUSAL THAT MATTERS. Over the null corpus the engine answers ok=false;
    ' a model asked to name the columns of a REFUSED specification will name
    ' them fluently, and those names would be the most convincing part of a
    ' result the engine had already thrown out. Structural, not incidental --
    ' today there are no fields to name, and that would stop being true the
    ' moment the engine reported low confidence instead of refusing.
    nulls = []
    for each nf in list_files("examples/fixtures/ari_discover/null")
        append(nulls, { id: file_name(nf), text: read(nf) })
    end for
    np = ari_discover.infer(nulls)
    check("the engine refuses the null corpus", np.ok, false)
    on error goto next
    bad4 = ari_advisor.advise_names(np, m)
    if error then
        check("and the advisor refuses to name it", contains(error.message, "nothing established to name"), true)
        error.clear()
    else
        check("and the advisor refuses to name it", "named it", "refused")
    end if

    ' THE CONTROL for that refusal: an ESTABLISHED proposal is still accepted,
    ' or "refuses a refused proposal" is satisfied by refusing everything.
    ok2 = ari_advisor.advise_names(p, m)
    check("CONTROL: an established proposal is still advised", ok2.advisor.answered, true)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.bad)
end program
