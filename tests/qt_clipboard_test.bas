' THE CLIPBOARD (docs/qt_gui_design.md §0.33) -- `qt.copy(text)` and
' `qt.clipboard()`. Under QT_QPA_PLATFORM=offscreen: a real QClipboard, no
' display.
'
' REPORTED FROM THE gBASIC STUDIO BENCH as one of two gaps that stopped STU-21
' being ported at all -- its tab menu offers `Close` and `Copy file path`, and
' the second is the item the user volunteered an opinion about ("the
' right-click and copy file path works, and I really like it"). There was no
' clipboard verb in this module at all.
'
' THE MEASUREMENT STUDIO ASKED FOR IS THE TIER TO READ FIRST, and it is the
' `dead frame` one. Under GTK `clipboard.set_text` HANGS, and the route that
' works installs a provider that reads a buffer AT PASTE TIME -- so a buffer
' that goes out of scope gives a successful-looking copy and an EMPTY PASTE,
' with nothing raised at any layer; Studio holds its buffer on a global for
' that reason alone. Their own words: "worth one measurement either way --
' copy, then read it back after the frame that copied has returned." Measured,
' Qt copies EAGERLY, so the trap does not exist here; this fixture is what
' keeps that true, because the failure it guards against is invisible from
' inside the program that copied.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here produces an ORDINARY
' RUN. A copy that silently did nothing still returns true; a read that
' answered "" for an empty clipboard still looks like a string; a text
' shortened at an interior NUL is a plausible shorter string. None of them
' raises, so a golden would record the damage as expected.
'
' NO WINDOW IS OPENED UNTIL THE LAST TIER, deliberately: there is ONE clipboard
' per application, so a verb needing a window would be claiming otherwise, and
' that claim is only checkable by not opening one.

function check(what, got, want)
    g.checks = g.checks + 1
    if string(got) = string(want) then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' THE FRAME THAT COPIES AND THEN RETURNS. Its argument and every local it
' builds are gone by the time anything reads the clipboard back, which is
' exactly the shape that fails under GTK.
function copy_from_a_frame()
    buffer = "hello from a dead frame"
    ok = qt.copy(buffer)
    return ok
end function

window app( title: "Clipboard", width: 320, height: 200 )
    column
        button go( text: "Go" )
    end column
end window

program main()
    g = { checks: 0, bad: 0 }

    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if

    ' WITH NO WINDOW OPEN AT ALL, which is the structural claim: this and every
    ' check before the last tier runs before `qt.open` is ever called.
    check("nothing has been copied, so there is nothing to read",
          is_unknown(qt.clipboard()), true)

    check("a copy reports it happened", qt.copy("plain text"), true)
    check("and reads back", qt.clipboard(), "plain text")

    ' THE DEAD-FRAME TIER. If Qt copied lazily the clipboard would hold a
    ' reference to a buffer that no longer exists and this would read back
    ' empty, with the copy still having reported success.
    check("the copy reported success", copy_from_a_frame(), true)
    check("a copy survives the frame that made it",
          qt.clipboard(), "hello from a dead frame")

    ' NOTHING AND EMPTY ARE DIFFERENT ANSWERS, which Qt distinguishes rather
    ' than this backend guessing: a fresh clipboard has no text, and a
    ' deliberately copied empty string has text of length 0. Without this the
    ' first check in the file is satisfied by a reader that answers `unknown`
    ' for everything it cannot be bothered with.
    nothing_copied = qt.copy("")
    check("an empty string copies", nothing_copied, true)
    check("and is TEXT, not absence", is_unknown(qt.clipboard()), false)
    check("and is empty", qt.clipboard(), "")

    ' A NUL IS CONTENT (PLAT-NUL). The naive implementation measures the text
    ' with strlen, puts one byte on the clipboard and reports success -- a
    ' shortened copy nothing can see from here, which is why the check is the
    ' LENGTH and a byte rather than the string's rendering.
    three = "a" + chr(0) + "b"
    check("the subject really is three bytes", len(three), 3)
    ok3 = qt.copy(three)
    back = qt.clipboard()
    check("a copy carrying a NUL is not shortened", len(back), 3)
    ' AS HEX RATHER THAN BYTE BY BYTE, which is PLAT-NUL's own convention and
    ' is also what keeps the tier reporting: `byte_at(back, 1)` on a shortened
    ' string RAISES, so a truncation killed the fixture and the checks after it
    ' never ran -- one mismatch and an exit 1 where three facts were available.
    check("and the bytes are the ones that went in", hex_encode(back), "610062")

    ' MULTI-LINE AND NON-ASCII, because a file path is not the only thing
    ' anybody copies and an editor's selection is both.
    sample = "first line" + chr(10) + "héllo ☃"
    ok4 = qt.copy(sample)
    check("multi-line and non-ASCII text round-trips", qt.clipboard(), sample)

    ' A PATH MAY ARRIVE AS A REFERENCE, for the reason §0.16's chooser options
    ' do: copying the path of a file is the use this verb was asked for, and
    ' what a program holds after choosing one is a `{file}`.
    f {file}= "/etc/hostname"
    ok5 = qt.copy(f)
    check("a file reference copies as its path", qt.clipboard(), "/etc/hostname")
    d {dir}= "/tmp"
    ok6 = qt.copy(d)
    check("and so does a folder reference", qt.clipboard(), "/tmp")

    ' REFUSALS, each beside its nearest legal neighbour.
    on error goto next
    bad1 = qt.copy()
    if error then
        check("qt.copy with nothing to copy is refused", error.message,
              "qt.copy expects the text to copy")
    else
        print "MISMATCH qt.copy() was accepted"
    end if

    on error goto next
    bad2 = qt.copy(42)
    if error then
        check("a number is refused by name", error.message,
              "qt.copy expects text, or a file or folder reference")
    else
        print "MISMATCH qt.copy(42) was accepted"
    end if

    on error goto next
    bad3 = qt.clipboard("put this")
    if error then
        check("qt.clipboard takes no arguments", error.message,
              "qt.clipboard takes no arguments -- it answers what is on the " +
              "clipboard, and qt.copy(text) puts something there")
    else
        print "MISMATCH qt.clipboard(text) was accepted"
    end if

    ' AND THE CONTROL FOR ALL THREE: the clipboard still works afterwards, or
    ' "it refuses" would be satisfied by a verb that refuses everything.
    ok7 = qt.copy("after the refusals")
    check("a refusal does not break the clipboard", qt.clipboard(),
          "after the refusals")

    ' WITH A WINDOW OPEN IT IS THE SAME CLIPBOARD, which is the other half of
    ' "no window argument": the verb must not quietly mean something different
    ' once a window exists, and `gb_qt_begin` being idempotent is what that
    ' rests on.
    opened = qt.open(app)
    check("a window opened", opened, true)
    check("the clipboard still holds what was copied before it",
          qt.clipboard(), "after the refusals")
    ok8 = qt.copy("copied with a window up")
    check("and copying still works", qt.clipboard(), "copied with a window up")
    shut = qt.close(app)
    check("the clipboard outlives the window", qt.clipboard(),
          "copied with a window up")

    print "ok   " + string(g.checks) + " checks, " + string(g.bad) + " mismatches"
end program
