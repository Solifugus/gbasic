' bytes / lines / chars over a file -- and the one of the three that was wrong.
'
' `chars(f)` COUNTED BYTES. The implementation shared a branch with `bytes`
' under a `TODO: chars currently counts bytes, not Unicode code points.`, so the
' two verbs returned the IDENTICAL NUMBER on every file -- right for ASCII and
' wrong for everything else, in the direction that looks like a working answer.
' `len(read(f))` was already correct, so the two routes to the same question
' disagreed and nothing said which. Reported by the gbasic-books session
' 2026-10-02.
'
' AND `bytes(f)` READ THE WHOLE FILE to return a number `stat` gives for free,
' because it shared the read path too -- gigabytes of allocation to answer how
' large something is.
'
' SELF-CHECKING, and here it is forced twice over: a count is a PLAUSIBLE SMALL
' INTEGER, so a golden would have recorded 13 as the character count of a
' 12-character file and defended it -- which is exactly what happened, except
' that there was no golden at all. `chars` had NO TEST COVERAGE IN THE TREE,
' which is how a TODO in the source survived to a release.

function check(label, got, want)
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
    return nothing
end function

program main(args)
    f {file}= "examples/tmp_file_counts.txt"

    ' "h" + e-acute + "llo world\n": 13 BYTES, 12 CODEPOINTS, 1 line.
    accent = from_bytes([195, 169])
    write(f, "h" + accent + "llo world" + chr(10))

    check("bytes counts bytes     ", bytes(f), 13)
    check("chars counts codepoints", chars(f), 12)
    check("lines counts lines     ", lines(f), 1)

    ' THE LOAD-BEARING CHECK IS THE DIFFERENCE. Every value check above is also
    ' satisfied by a build where chars and bytes are the same function, as long
    ' as somebody writes 13 in both places -- so the assertion that separates a
    ' fixed `chars` from the old one is that the two verbs DISAGREE here.
    check("the two verbs differ   ", chars(f) != bytes(f), true)

    ' AND THE SECOND ROUTE AGREES. `len` on the file's contents answers the same
    ' question through code that was already correct, so it is an ORACLE rather
    ' than a second call into the thing under test.
    t = read(f)
    check("chars agrees with len  ", chars(f), len(t))
    check("bytes agrees with count", bytes(f), byte_count(t))

    ' CONTROL: on a pure-ASCII file the two verbs MUST agree, or "chars counts
    ' codepoints" would be satisfied by a chars that returns anything else --
    ' an off-by-one, a line count, a constant.
    g {file}= "examples/tmp_file_counts_ascii.txt"
    write(g, "abc" + chr(10))
    check("ASCII: bytes = 4       ", bytes(g), 4)
    check("ASCII: chars = 4 too   ", chars(g), 4)
    check("ASCII: the two agree   ", chars(g) = bytes(g), true)

    ' CONTROL: an EMPTY file is 0 both ways, not 1 and not unknown.
    e {file}= "examples/tmp_file_counts_empty.txt"
    write(e, "")
    check("empty: bytes = 0       ", bytes(e), 0)
    check("empty: chars = 0       ", chars(e), 0)
    check("empty: lines = 0       ", lines(e), 0)

    delete(f)
    delete(g)
    delete(e)
end program
