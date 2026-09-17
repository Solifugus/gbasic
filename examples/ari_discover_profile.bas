' Profile one print-image report: what repeats, what varies, what is furniture.
'
' Phase 0 PROPOSES NOTHING. It measures, and the measurement is the deliverable:
' you read it and decide whether the corpus is one a specification can be
' written for, before anyone writes one.
'
'   GBASIC_PATH=stdlib ./gbasic examples/ari_discover_profile.bas [report.rpt]
program main( args )
    load ari_discover

    path = "examples/fixtures/ari_discover/01_branch_activity.rpt"
    if count(args) > 0 then
        path = args[0]
    end if
    f {file}= path
    p = ari_discover.profile(read(f))

    print ari_discover.explain(p)
    print ""

    ' The furniture report says what it found AND, when it cannot tell, why.
    ' A single-page report's header is indistinguishable from its first
    ' heading, so the honest answer there is a refusal rather than a guess.
    if p.furniture.why != "" then
        print ("furniture: " + p.furniture.why)
    else
        print ("furniture: " + string(count(p.furniture.lines))
               + " lines over " + string(p.furniture.pages)
               + " pages, by " + p.furniture.evidence)
        for each off in p.furniture.offsets
            print ("  offset " + string(off.offset) + "  " + off.shape)
        end for
    end if
end program
