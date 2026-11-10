' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' source_comments(text) by hand (Qt design §0.38 B1): every expected run below
' is written out, positions included, from the fixture text itself. What the
' parser reads as something else -- a string, an SQL body -- must not be a
' comment, and the corpus tier in tests/run_source_comments.sh holds the rest.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function joined(list)
    return join(list, "\n")
end function

' A run as one line of text, the fields Studio reads.
function shape(c)
    return (string(c.start_line) + "-" + string(c.end_line) + "@" + string(c.start_column)
            + " " + string(c.trailing) + " [" + c.text + "]")
end function

function shapes(text)
    out = []
    for each c in source_comments(text).comments
        append(out, shape(c))
    next
    return out
end function

program main(args)
    G = { checks: 0, mismatches: 0 }

    print "-- runs"
    src = joined(["' Rates", "' are read here", "x = 1", "", "' one", "", "' two"])
    check("consecutive comment lines are one run; a line of code or a blank ends it",
          shapes(src), ["1-2@1 false [ Rates\n are read here]", "5-5@1 false [ one]", "7-7@1 false [ two]"])
    src = joined(["    ' indented", "    ' run", "' outdented"])
    check("a change of column ends a run", shapes(src), ["1-2@5 false [ indented\n run]", "3-3@1 false [ outdented]"])
    src = joined(["x = 1 ' after code", "' alone"])
    check("a comment after code is trailing, and a run of its own",
          shapes(src), ["1-1@7 true [ after code]", "2-2@1 false [ alone]"])
    src = joined(["'' doc marker", "'' second", "'"])
    r = source_comments(src).comments
    check("one quote is removed per line; Studio's own marker stays", r[0].text, "' doc marker\n' second\n")
    check("raw keeps every marker", r[0].raw, "'' doc marker\n'' second\n'")
    check("offsets: the run begins at its quote", [r[0].start_offset, r[0].end_offset], [0, byte_count(src)])

    print ""
    print "-- what is not a comment"
    src = joined(["x = \"it's here\" ' real", "y = \"'\"", "print x + y"])
    check("a quote inside a string is not a comment", shapes(src), ["1-1@17 true [ real]"])
    src = joined(["database shop( file: \"x.gsql\", create: true )", "sql q on shop",
                 "    select 'it''s' as a -- ' in SQL", "end sql", "' after the block"])
    r = source_comments(src)
    check("the text parses", r.ok, true)
    check("a quote inside an SQL body is SQL, not a comment", shapes(src), ["5-5@1 false [ after the block]"])
    src = joined(["total = (1 +   ' inside brackets", "         2)", "print total"])
    check("a comment inside an unclosed bracket is still a comment", shapes(src), ["1-1@16 true [ inside brackets]"])

    print ""
    print "-- bytes"
    src = "' crlf" + chr(13) + "\nx = 1" + chr(13) + "\n"
    check("CRLF: the CR is not part of the comment", shapes(src), ["1-1@1 false [ crlf]"])
    src = joined(["s = \"Łódź\" ' after", "' end of file, no newline"])
    check("columns are bytes, as source_outline's are", shapes(src), ["1-1@15 true [ after]", "2-2@1 false [ end of file, no newline]"])
    ' The lexer counted a modifier's `{` twice, so everything after one was a
    ' column to the right (fixed with this builtin, 2026-11-10).
    check("after a one-word modifier the column is right", shapes("d {datetime}= \"x\" ' c"), ["1-1@19 true [ c]"])
    check("after an inline one", shapes("d = {USD}\"2\" ' c"), ["1-1@14 true [ c]"])
    check("after a multi-word one", shapes("d = {end of month}x ' c"), ["1-1@21 true [ c]"])
    check("an empty text has none", source_comments("").comments, [])
    check("nor a text with no comments", source_comments("x = 1\nprint x").comments, [])

    print ""
    print "-- a text that does not parse"
    r = source_comments(joined(["' before", "x = (", "' inside the open bracket", "print"]))
    check("ok is false", r.ok, false)
    check("the diagnostics say why", count(r.diagnostics) > 0 and r.diagnostics[0].severity = "error", true)
    check("the comments read before the error are answered", count(r.comments) >= 1 and r.comments[0].text = " before", true)

    print ""
    print "-- refusals"
    on error goto next
    x = source_comments(42)
    check("not text", contains(error.message, "source_comments expects a string"), true)
    error.clear()
    x = source_comments()
    check("no argument", contains(error.message, "source_comments expects one argument"), true)
    error.clear()
    on error stop

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
