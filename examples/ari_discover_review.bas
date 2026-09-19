' PHASE 3: the review loop -- variants, decisions, and an account of every rule.
'
' Phases 0-2 answer "what specification does this corpus support". This one
' answers the two questions a person asks next: IS THIS ONE REPORT OR SEVERAL,
' and WHAT WOULD I CHANGE. Both are run here against real corpora rather than
' described.
'
' The variant question is the one that goes wrong quietly. Inference is a
' search, a search always returns a winner, and a variant detector pointed at a
' corpus that merely DRIFTS will report variants -- which looks exactly like a
' discovery. So the first thing this program does is point it at 24 sources
' that vary nine axes and show it declining to split them.
'
'   GBASIC_PATH=stdlib ./gbasic examples/ari_discover_review.bas

load ari_discover

function slurp(path)
    f {file}= path
    return read(f)
end function

function load_corpus(dirpath, prefix, suffix, n)
    out = []
    i = 1
    while i <= n
        id = prefix + replace(mid(string(100 + i), 1, 2), " ", "0")
        append(out, { id: id, text: slurp(dirpath + id + suffix) })
        i = i + 1
    end while
    return out
end function

function report(label, v)
    print ("  " + label)
    print ("     grammars        " + string(count(v.groups)))
    for each g in v.groups
        print ("       " + padn(string(g.n), 3) + "  " + g.grammar)
    end for
    print ("     one spec serves " + string(v.one.served) + " / "
           + string(v.one.sources))
    if not is_unknown(v.split) then
        print ("     split serves    " + string(v.split.served) + " / "
               + string(v.split.sources))
    end if
    print ("     ANSWER          " + v.recommendation)
    print ("     " + v.why)
    print ""
    return nothing
end function

function padn(s, w)
    n = w - len(s)
    if n <= 0 then
        return s
    end if
    return repeat(" ", n) + s
end function

program main( args )
    dir = "examples/fixtures/ari_discover/"

    branches = load_corpus(dir, "", "_branch_activity.rpt", 24)
    ' The ids the loader builds are "01".."24"; the files carry the long name.
    bi = 0
    while bi < count(branches)
        branches[bi].id = branches[bi].id + "_branch"
        bi = bi + 1
    end while

    tellers = load_corpus(dir + "variants/", "t", "_teller_session.rpt", 5)

    print "=========================================================="
    print " §13  IS THIS ONE REPORT OR SEVERAL?"
    print "=========================================================="
    print ""

    ' THE NEGATIVE CONTROL FIRST. These 24 sources differ in money notation,
    ' indent, date dialect, description width, the total's label and the way
    ' they paginate -- and none of that changes what the specification must
    ' SAY, which is the only thing that makes a difference material.
    report("24 branch-activity registers, nine drift axes:",
           ari_discover.variants(branches))

    ' The same question where the answer really is two. The detail row's own
    ' grammar differs -- different arity, different types, no date at all -- and
    ' no alternation can carry that, because a field one form does not have
    ' cannot be written into a rule shared with it.
    mixed = []
    mi = 0
    while mi < 8
        append(mixed, branches[mi])
        mi = mi + 1
    end while
    for each t in tellers
        append(mixed, t)
    end for
    report("8 of those plus 5 teller session journals:",
           ari_discover.variants(mixed))

    ' And the same question with the evidence declared thin. §13's third remedy
    ' is neither one specification nor several: it is more samples.
    report("the same corpus, if a variant needs 6 sources:",
           ari_discover.variants(mixed, { minimum_variant_sources: 6 }))

    print "=========================================================="
    print " §10  WHAT WOULD I CHANGE?"
    print "=========================================================="
    print ""

    eight = []
    ei = 0
    while ei < 8
        append(eight, branches[ei])
        ei = ei + 1
    end while

    p = ari_discover.infer(eight)
    print ("proposed specification, " + string(count(p.fields))
           + " detail field(s), " + string(count(p.questions)) + " open question(s)")
    print ""

    ' A review is a LIST OF RECORDS, and that is the whole point: it goes
    ' through `encode` into a file, comes back through `decode`, and reproduces
    ' the same specification. A review that only worked in memory is a function
    ' call, not a record of a decision.
    review = [ { decision: "rename_field", field: p.fields[0].name,
                 to: "posted_on" },
               { decision: "field_type", field: p.fields[1].name, as: "text" } ]
    on_the_wire = encode(review)
    print ("the review, written down (" + string(len(on_the_wire)) + " bytes):")
    print ("  " + on_the_wire)
    print ""

    revised = ari_discover.refine(eight, p, decode(on_the_wire))
    print "the specification it produces:"
    print ""
    for each ln in split(revised.spec, "\n")
        print ("  " + ln)
    end for
    print ""

    ' A decision that matches nothing is REFUSED rather than ignored, because a
    ' silently dropped rename leaves the reviewer believing they made a change
    ' they did not -- and the next thing they do is trust the specification.
    on error goto next
    oops = ari_discover.refine(eight, p,
                               [ { decision: "rename_field", field: "acct_no",
                                   to: "account" } ])
    if error then
        print "a decision that matches nothing is refused, not ignored:"
        print ("  " + error.message)
        error.clear()
    end if
    on error stop
    print ""

    print "=========================================================="
    print " §12  WHY DOES EACH RULE SAY WHAT IT SAYS?"
    print "=========================================================="
    print ""
    print ari_discover.explain(p)
    print ""

    print "=========================================================="
    print " AND THE INVERSE: WHAT DID IT NEVER SEE?"
    print "=========================================================="
    ' `explain` accounts for every rule the specification HAS. This is the other
    ' half, and it is the one a reviewer acts on: text on the page that no rule
    ' reads. It REPORTS -- nothing here proposes a field. Whether any of these
    ' shapes SHOULD be read is the reviewer's call, which is the whole point of
    ' showing them.
    print ""
    print ari_discover.explain(ari_discover.unexplained(eight, p.spec))
end program
