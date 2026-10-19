' A FILE CHOOSER (§0.16) -- under QT_QPA_PLATFORM=offscreen: a real
' QFileDialog, a real nested modal loop, no display.
'
' SELF-CHECKING NOT GOLDEN AND FORCED. Every defect here is an
' ORDINARY-LOOKING PATH: an option that silently did nothing opens the chooser
' in the wrong folder with the wrong filter and says so nowhere, and a
' cancelled chooser that answered a path rather than `unknown` hands back a
' file the person declined -- which is not an error, it is a wrong answer, and
' a golden would record it as expected and defend it.
'
' THE TIER TO READ FIRST IS `cancel`. Measured while building this: a REJECTED
' QFileDialog STILL REPORTS ITS SELECTION, so the obvious implementation --
' read selectedFiles() when the dialog closes -- returns the path for a chooser
' the person cancelled. Only the result code separates the two.
'
' AND THAT TIER HAD TO BE MADE ABLE TO SEE IT, which a perturbation found and
' reading did not: a cancel with nothing preselected answers `unknown` either
' way, because the selection is empty either way -- so the check whose comment
' claimed to catch the defect did not, and an unrelated tier caught it. It
' opens the chooser ON A FILE now, which is also the realistic shape.
'
' AND THE ORACLE WAS BUILT BEFORE THE FIXTURE, which is the discipline §0.8
' arrived at the hard way: `title:` and `filter:` are otherwise ACCEPTED AND
' UNOBSERVABLE, the hole a leaf's `label:` sat in for four increments while
' every check passed. `qt.dialog()` is what makes them assertable.
'
' WHAT MAKES ANY OF THIS DRIVABLE WITHOUT A PERSON is that gBASIC's own
' sources are serviced while the nested loop is up, so a `timer` can answer the
' program's own chooser -- and `qt.answer` does it the way a person does, by
' typing into the chooser's name field and pressing its Open button, because
' `selectFile` is IGNORED while the dialog is visible (measured: Qt will not
' overwrite what somebody is typing).

' THE INSTRUCTION IS A MODE, not an overloaded field. `g.want` held a path,
' the word "cancel" and `true` in the first draft, so the driver compared a
' boolean to a string -- which PLAT-EQ answers `false` for rather than raising,
' so every case silently took the cancel branch and the first choice came back
' `unknown`. One field, one kind.
function drive()
    if g.mode = "cancel" then
        ok = qt.answer(false)
        return nothing
    end if
    if g.mode = "true" then
        on error goto refused_true
        ok = qt.answer(true)
        return nothing
refused_true:
        g.refusal = error.message
        error.clear()
        ok = qt.answer(false)
        return nothing
    end if
    on error goto refused
    ok = qt.answer(g.want)
    return nothing
refused:
    g.refusal = error.message
    error.clear()
    ok = qt.answer(false)
    return nothing
end function

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

window app( title: "Chooser", width: 320, height: 140 )
    column( margin: 10 )
        text t( label: "pick something" )
    end column
end window

program main(args)
    g = { mode: "path", want: "", seen: {}, refusal: "", ticks: 0 }

    if not qt.can_display() then
        print "SKIP no display"
        return
    end if
    work = args[0]
    here = work + "/have.txt"
    absent = work + "/not-there.txt"

    qt.open(app)

    print "-- answering with a path when nothing is open is refused"
    on error goto nothing_open
    x = qt.answer(here)
    print "MISMATCH a path was accepted with no dialog open"
    return
nothing_open:
    check("refused, naming the state", contains(error.message, "no dialog is open"), true)
    error.clear()

    print "-- and qt.dialog() says so rather than inventing a record"
    ' `unknown` RATHER THAN AN EMPTY RECORD, the distinction `principal()`
    ' already makes: "nothing is open" and "a dialog carrying no fields" are
    ' different claims.
    check("nothing open answers unknown", is_unknown(qt.dialog()), true)

    ' THE DRIVER. One tick reads what is open, then answers it according to
    ' `g.want` -- so every case below is the same mechanism with a different
    ' instruction, and a case that stopped reaching the chooser would hang
    ' rather than quietly pass.
    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            g.ticks = g.ticks + 1
            d = qt.dialog()
            if not is_unknown(d) then
                g.seen = d
                drive()
            end if
        end while
    end watch

    print "-- choosing a file that is there"
    g.mode = "path"
    g.want = here
    chosen = qt.choose_file(app, { title: "Open a thing", folder: work, filter: ["Text (*.txt)", "All (*)"] })
    check("the path comes back", chosen, here)
    check("as text, so it composes with {file}", type(chosen), "string")
    check("and the file really reads", read({file}chosen), "hello" + chr(10))

    print "-- and the options reached the chooser, not just the record"
    ' THE HALF A VALUE CHECK CANNOT MAKE: `setProperty`-style options that are
    ' accepted and dropped look exactly like options that worked.
    check("title:", g.seen.title, "Open a thing")
    check("folder:", g.seen.folder, work)
    ' A LIST JOINED WITH QT'S OWN `;;`, which is the thing an author should not
    ' have to know -- asserted because joining in the wrong place yields one
    ' filter reading `Text (*.txt), All (*)`, which matches nothing and looks
    ' like a filter.
    check("filter: as a list", g.seen.filter, "Text (*.txt);;All (*)")
    check("and it is a chooser, not a message box", g.seen.kind, "chooser")

    print "-- a path option may be a reference, not only text"
    ' A GAP RATHER THAN A RULE, and the cookbook found it: `temp_dir()` answers
    ' a `{dir}`, so the most natural `folder:` there is was refused as "not
    ' text". Every path builtin in this language already takes a string, a
    ' `{file}` or a `{dir}` alike, and a GUI module is the wrong place to be
    ' stricter -- so `qt.answer` takes one too, asserted here because the
    ' chooser and the answer are two separate doors and a fix reaching one is
    ' the shape webclient's read and write halves had.
    g.want = {file}here
    by_ref = qt.choose_file(app, { folder: {dir}work })
    check("a {dir} folder and a {file} answer", by_ref, here)

    print "-- naming a file to write, which need not exist yet"
    g.want = work + "/fresh.txt"
    saving = qt.choose_save(app, { title: "Save as", folder: work, name: "fresh.txt" })
    check("a name for a file that is not there", saving, work + "/fresh.txt")

    print "-- choosing a folder"
    g.want = work
    folder = qt.choose_folder(app, { title: "Pick a folder", folder: work })
    check("the folder comes back", folder, work)

    print "-- cancelling answers unknown, never \"\""
    ' THE LOAD-BEARING TIER. A rejected QFileDialog still reports its
    ' selection, so a backend reading the list without checking the result code
    ' hands back the path for a chooser the person declined. And `unknown`
    ' rather than "" because the empty string is the plausible wrong answer:
    ' `if path = "" then` happens to work while `read({file}path)` does not.
    ' THE CHOOSER IS OPENED ON A FILE, which is both the realistic shape -- a
    ' chooser opens on the file you had, and the person changes their mind --
    ' and what makes this tier able to see the defect at all. MEASURED by
    ' perturbation: with the result code ignored, a cancel with NOTHING
    ' preselected still answers `unknown`, because the selection is empty
    ' either way, so the tier whose comment claimed to catch it did not and
    ' the absent-path tier below caught it instead. `name:` is applied BEFORE
    ' exec, where selectFile is still honoured, so there is a selection for a
    ' result-code-ignoring backend to hand back.
    g.mode = "cancel"
    none = qt.choose_file(app, { folder: work, name: "have.txt" })
    check("cancel is unknown", is_unknown(none), true)
    check("and not the empty string", none = "", false)
    check("and not the file it opened on", none = here, false)
    ' ASSERTED AS A DIFFERENCE, since "it answers unknown" alone passes on a
    ' chooser that never answers anything.
    g.mode = "path"
    g.want = here
    again = qt.choose_file(app, { folder: work })
    check("while a choice is still a path", again, here)

    print "-- a path this chooser cannot take is refused, not hung"
    ' MEASURED: Qt DISABLES the Open button for an absent file in an
    ' `ExistingFile` chooser, so the refusal is the chooser's own rule. Calling
    ' accept() instead would put up Qt's own warning box, which opens a SECOND
    ' nested loop from inside the first one's pump tick -- and Qt will not
    ' re-enter a timer whose handler is on the stack, so nothing could reach
    ' either dialog. That is a hang, and a hang is not a failure.
    g.refusal = ""
    g.want = absent
    gave_up = qt.choose_file(app, { folder: work })
    check("refused, naming the path", contains(g.refusal, "not-there.txt"), true)
    check("and saying it is not there", contains(g.refusal, "not there"), true)
    check("so the chooser ended cancelled rather than hanging", is_unknown(gave_up), true)

    print "-- true does not say which file"
    ' `false` IS A CANCEL, which is meaningful and is what a timeout wants to
    ' say; `true` would have to mean "accept what?" and guessing which file
    ' somebody meant is the one thing this verb must not do.
    g.refusal = ""
    g.mode = "true"
    nope = qt.choose_file(app, { folder: work })
    check("refused, naming the remedy", contains(g.refusal, "give qt.answer the path"), true)

    cancelled = timer.cancel(beat)
    qt.stop()
end program
