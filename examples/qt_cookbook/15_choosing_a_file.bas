' CHOOSING A FILE. Three verbs, because they ask three different questions --
' which existing file, what shall I call it, which folder -- and each refuses
' the options that mean nothing to it rather than ignoring them.
'
' THE ANSWER IS TEXT, NOT A {file}. Three sibling verbs would otherwise answer
' two kinds, and a chooser has no declaration to read a kind from: what Qt
' holds is a path. So `{file}` goes where the value is used, which is one
' inline modifier.
'
' CANCELLING ANSWERS `unknown`, NEVER "". The empty string is the plausible
' wrong answer -- `if path = "" then` happens to work while `read({file}path)`
' does not -- so `unknown` is what forces you to notice, and `if path then` on
' it raises rather than quietly taking the wrong branch.
'
' A RECIPE HAS NOBODY TO CLICK, so a timer answers for one. `qt.answer` takes
' the PATH here rather than true or false, because that is what a chooser's
' answer is; `false` is a cancel, and `true` is refused, since it would have to
' mean "accept what?".
'
' THE SCRATCH DIRECTORY IS THE RECIPE'S OWN, so this prints the same thing on
' every machine. A real program would start wherever the user last was.
window app( title: "Choosing a file", width: 380, height: 160 )
    column( margin: 10, spacing: 8 )
        text state( label: "nothing chosen yet" )
    end column
end window

program main(args)
    g = { mode: "path", want: "" }

    if not qt.can_display() then
        print "no display"
        return
    end if

    work = temp_dir()
    report = write({file}(work + "/report.txt"), "the quarterly numbers" + chr(10))

    qt.open(app)

    beat = timer.every(0.05)
    watch(timer.ticks)
        while count(timer.ticks) > 0
            ev = take_first(timer.ticks)
            if g.mode = "cancel" then
                ok = qt.answer(false)
            else
                ok = qt.answer(g.want)
            end if
        end while
    end watch

    ' WHICH EXISTING FILE. A list of filters is joined with Qt's own `;;` for
    ' you, which is a spelling nobody should have to know.
    g.want = work + "/report.txt"
    chosen = qt.choose_file(app, { title: "Open a report", folder: work,
        filter: ["Text files (*.txt)", "All files (*)"] })
    print "chose    " + file_name(chosen)
    print "and it reads " + string(len(read({file}chosen))) + " bytes"

    ' WHAT SHALL I CALL IT. The file need not exist yet, which is the whole
    ' difference from the verb above.
    g.want = work + "/summary.txt"
    saving = qt.choose_save(app, { title: "Save as", folder: work,
        name: "summary.txt" })
    print ("saving   " + file_name(saving) + ", exists already: " +
        string(exists({file}saving)))
    done = write({file}saving, "a summary" + chr(10))

    ' WHICH FOLDER.
    g.want = work
    folder = qt.choose_folder(app, { title: "Where to put things",
        folder: work })
    print "folder   has " + string(count(files({dir}folder, "*.txt"))) + " text files"

    ' AND WHAT A CANCEL LOOKS LIKE, which is the branch every program needs.
    g.mode = "cancel"
    none = qt.choose_file(app, { folder: work })
    if is_unknown(none) then
        print "cancelled, so nothing to open"
    else
        print "cancelled, but got " + none
    end if

    cancelled = timer.cancel(beat)
    gone = remove_tree({dir}work)
    qt.stop()
end program
