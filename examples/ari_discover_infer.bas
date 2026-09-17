' PHASE 1: infer a specification from a corpus, and let `ari` judge it.
'
' Run twice, with one thing different: whether positional (`columns`) rules are
' permitted. The difference is the design's own principle -- relative structure
' before columns -- as a measurement rather than an admonition.
'
' Anchor-relative fields survive layout drift. Positional ones do not, and they
' fail SILENTLY: every source still parses, nothing is unknown, and the values
' are ordinary-looking account numbers from the wrong columns. `anchor_stability`
' is the only measure that catches it, and it needs no answer key -- it asks
' whether the family's column structure is the same in every source.
'
'   GBASIC_PATH=stdlib ./gbasic examples/ari_discover_infer.bas [count]
program main( args )
    load ari_discover

    dir = "examples/fixtures/ari_discover/"
    want = 8
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

    for each allow in [ false, true ]
        p = ari_discover.infer(sources, { allow_fixed_columns: allow })
        print ("=== allow_fixed_columns: " + string(allow) + " ===")
        if not p.ok then
            print ("  refused: " + p.why)
            print ""
            continue
        end if
        print p.spec
        print ""
        print ("  sources parsed        " + string(p.scorecard.parsed)
               + "/" + string(p.scorecard.sources))
        print ("  rows extracted        " + string(p.scorecard.rows))
        print ("  unknown rate          " + string(p.scorecard.unknown_rate))
        print ("  positional dependence " + string(p.scorecard.positional_dependence))
        print ("  anchor stability      " + string(p.scorecard.anchor_stability)
               + "   (" + string(p.scorecard.layouts) + " distinct column layouts)")
        print ("  not computable        " + join(p.scorecard.not_computable, ", "))
        for each q in p.questions
            print ("  QUESTION [" + q.field + "] " + q.why)
            for each opt in q.options
                print ("      - " + opt)
            end for
        end for
        print ""
    end for
end program
