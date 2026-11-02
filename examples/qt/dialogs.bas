' DIALOGS -- a statement, a question, three file choosers, and a window shown
' modally. The one example here you can sit in front of and click.
'
' Run it:
'     ./gbasic examples/qt/dialogs.bas
'     QT_QPA_PLATFORM=offscreen ./gbasic examples/qt/dialogs.bas check
'
' WHY THIS EXISTS. Everything dialog-shaped in this tree is driven by a TIMER,
' because that is the only way a suite can answer a dialog with nobody there --
' and the consequence is that none of it could be LOOKED AT: the cookbook
' recipes answer themselves in about fifty milliseconds and exit. Press the
' buttons below instead.
'
' THE TWO MODES RAISE THEIR DIALOGS FROM DIFFERENT PLACES, and that is the
' measured limit rather than an accident. A dialog opened from `main` leaves
' gBASIC's own sources running, so a `timer` can answer it -- which is what
' `check` does. A dialog opened FROM A HANDLER suspends every source until it
' is answered, so only a person can answer that one -- which is what the
' buttons do. Both are real; the second is the normal shape and the first is
' what makes any of it testable.
'
' SO `check` DOES NOT EXERCISE THE BUTTONS. It cannot: a timer cannot answer a
' dialog a click raised. Said here rather than left to be discovered.

window app( title: "Dialogs", width: 560, height: 420 )
    column( margin: 12, spacing: 10 )
        text intro( label: "Press a button. The answer appears in the log." )

        group "A statement and a question"
            row
                button say(     label: "Inform" )
                button ask(     label: "Confirm" )
                stretch()
            end row
        end group

        group "Choosing a file"
            row
                button pick(    label: "Open file..." )
                button save_as( label: "Save as..." )
                button folder(  label: "Choose folder..." )
                stretch()
            end row
        end group

        group "A window shown modally"
            row
                button edit_it( label: "Edit the customer..." )
                stretch()
            end row
        end group

        memo log( label: "Log", readOnly: true )
        row
            stretch()
            button quit( label: "Quit" )
        end row
    end column

    on say.clicked
        told = app.inform("Saved three records.")
        note("inform returned " + string(told))
    end on

    on ask.clicked
        ' NO IS THE DEFAULT BUTTON, so a question answered by pressing Return
        ' without reading is never the destructive one.
        if app.confirm("Delete the selected record?") then
            note("confirm: yes -- would delete")
        else
            note("confirm: no -- kept")
        end if
    end on

    on pick.clicked
        p = app.choose_file({ title: "Open a report", folder: g.work,
                                  filter: ["Text (*.txt)", "All files (*)"] })
        if is_unknown(p) then
            note("open: cancelled")
        else
            note("open: " + p + " (" + string(bytes({file}p)) + " bytes)")
        end if
    end on

    on save_as.clicked
        p = app.choose_save({ title: "Save the summary", folder: g.work,
                                  name: "summary.txt" })
        if is_unknown(p) then
            note("save: cancelled")
        else
            ' IT NEED NOT EXIST YET, which is the whole difference from
            ' `choose_file`.
            note("save: " + p + ", exists already: " +
                 string(exists({file}p)))
        end if
    end on

    on folder.clicked
        p = app.choose_folder({ title: "Pick a folder", folder: g.work })
        if is_unknown(p) then
            note("folder: cancelled")
        else
            note("folder: " + p)
        end if
    end on

    on edit_it.clicked
        ' THE SAME WINDOW AS ANY OTHER, shown a different way. `qt.open_modal`
        ' blocks until the form is answered and closes it on the way out, so
        ' `on edit.closed` is where its values are read.
        if edit.open_modal() then
            note("modal: saved " + g.typed.who + ", " + string(g.typed.limit))
        else
            note("modal: cancelled (the form said " + g.typed.who + ")")
        end if
    end on

    on quit.clicked
        qt.stop()
    end on

    on app.closed
        ' Closing the window by its own button is a close like any other.
        print "the window closed"
    end on
end window

window edit( title: "Customer", width: 340, height: 170 )
    column( margin: 12, spacing: 8 )
        form to edit cust
            field who(   label: "Name" )
            field limit( label: "Limit", type: {USD} )
        end form
        row
            stretch()
            button ok(     label: "Save" )
            button nothanks( label: "Cancel" )
        end row
    end column

    on ok.clicked
        x = edit.accept()
    end on
    on nothanks.clicked
        x = edit.reject()
    end on
    on edit.closed
        g.typed = edit.subject("cust")
    end on
end window

' A HANDLER CANNOT REBIND A VARIABLE IN `main` -- warning 2104 says so and
' names this remedy -- so the state is ONE RECORD whose fields handlers mutate,
' and appending to the log goes through here.
function note(line)
    g.lines = append(g.lines, line)
    x = app.write("log", join(g.lines, chr(10)))
    print line
    return nothing
end function

program main(args)
    g = { lines: [], work: "", typed: { who: "", limit: {USD}"0.00" },
          want: "yes", path: "" }

    if not qt.can_display() then
        print "No display is available, so there is no window to open."
        print "Set QT_QPA_PLATFORM=offscreen to render without one."
        return
    end if

    cust = { who: "Ada Lovelace", limit: {USD}"500.00" }
    g.work = temp_dir()
    made = write({file}(g.work + "/report.txt"), "the quarterly numbers" + chr(10))

    qt.open(app)
    x = app.write("log", "")

    if contains(args, "check") then
        ' THE TIMER ANSWERS WHATEVER IS OPEN. It only gets the chance because
        ' each dialog below is raised from `main` rather than from a handler.
        beat = timer.every(0.05)
        watch(timer.ticks)
            while count(timer.ticks) > 0
                ev = take_first(timer.ticks)
                if g.want = "accept" then
                    a = edit.accept()
                else
                    if g.want = "reject" then
                        a = edit.reject()
                    else
                        d = qt.dialog()
                        if not is_unknown(d) then
                            if d.kind = "chooser" then
                                if g.want = "cancel" then
                                    a = qt.answer(false)
                                else
                                    a = qt.answer(g.path)
                                end if
                            else
                                a = qt.answer(g.want = "yes")
                            end if
                        end if
                    end if
                end if
            end while
        end watch

        print "-- a statement"
        g.want = "yes"
        note("inform returned " + string(app.inform("Saved three records.")))

        print "-- a question, both ways"
        note("confirm yes: " + string(app.confirm("Delete?")))
        g.want = "no"
        note("confirm no:  " + string(app.confirm("Delete?")))

        print "-- choosing a file"
        g.want = "path"
        g.path = g.work + "/report.txt"
        chosen = app.choose_file({ folder: g.work })
        note("chose " + file_name({file}chosen) + ", " +
             string(bytes({file}chosen)) + " bytes")

        g.want = "cancel"
        none = app.choose_file({ folder: g.work })
        note("cancelled gives unknown: " + string(is_unknown(none)))

        print "-- a window shown modally"
        g.want = "accept"
        note("accepted: " + string(edit.open_modal()))
        note("the form said " + g.typed.who + ", " + string(g.typed.limit))

        g.want = "reject"
        note("rejected: " + string(edit.open_modal()))

        cancelled = timer.cancel(beat)
        gone = remove_tree({dir}g.work)
        qt.stop()
        return
    end if

    print "Press a button. Close the window or press Quit to finish."
    qt.run()
    gone = remove_tree({dir}g.work)
end program
