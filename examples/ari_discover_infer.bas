' Profile a CORPUS rather than one report.
'
' §5.1: variation across files is what separates a true constant from an
' accidental one. A signature that appears in one source is a fact about that
' source; one that appears in most of them is a fact about the report FAMILY,
' and only the second is something to build a specification on.
'
' Note what this prints when the corpus is too small. Below about ten sources
' `minimum_support` cannot be calibrated at all -- with three, 0.80 MEANS
' "3 of 3", because 2/3 is 0.67 -- and saying so beats reporting a support
' figure that can only take three values.
'
'   GBASIC_PATH=stdlib ./gbasic examples/ari_discover_infer.bas [count]
program main( args )
    load ari_discover

    dir = "examples/fixtures/ari_discover/"
    want = 12
    if count(args) > 0 then
        want = number(args[0])
    end if

    t {file}= dir + "truth.json"
    truth = decode(read(t))

    sources = []
    i = 0
    while i < want
        if i < count(truth.sources) then
            s {file}= dir + truth.sources[i].file
            append(sources, { id: truth.sources[i].id, text: read(s) })
        end if
        i = i + 1
    end while

    c = ari_discover.profile_corpus(sources)
    print ("corpus: " + string(c.sources) + " sources")
    if c.support_warning != "" then
        print ("  WARNING: " + c.support_warning)
    end if
    print ""

    print "signatures present in most sources -- candidates to build a rule on:"
    for each e in c.shared_signatures
        print ("  " + string(e.sources) + "/" + string(c.sources) + "  " + e.signature)
    end for
    print ""

    ' The other half, and the one a reader skips at their peril: a signature
    ' seen in exactly one source is either a genuine local variant or a
    ' mis-grouping, and Phase 0 does not claim to know which. It reports them
    ' so a person can look.
    print ("signatures seen in ONE source only: "
           + string(count(c.single_source_signatures)))
    shown = 0
    for each e in c.single_source_signatures
        if shown < 5 then
            print ("  " + e.signature)
            shown = shown + 1
        end if
    end for
    if count(c.single_source_signatures) > 5 then
        print ("  ... " + string(count(c.single_source_signatures) - 5) + " more")
    end if
end program
