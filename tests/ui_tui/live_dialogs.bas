' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 5 in a real terminal (tests/run_ui_tui.sh, tier live_dialogs):
' a question, a statement and the file chooser, each answered by the bytes
' a terminal sends for the keys and presses a person makes. Answers go to
' standard error; Quit ends the run. args[0] is the chooser's folder.
window app( title: "Live" )
    column
        button ask( label: "Ask" )
        button tell( label: "Tell" )
        button pick( label: "Pick" )
        button quit( label: "Quit" )
    end column
    on ask.pressed
        print to error "answer " + string(ui.confirm(app, "Sure?"))
    end on
    on tell.pressed
        print to error "told " + string(ui.inform(app, "Done."))
    end on
    on pick.pressed
        got = ui.choose_file(app, { folder: g.folder })
        if is_unknown(got) then
            print to error "picked nothing"
        else
            print to error "picked " + replace(got, g.folder, "PICK")
        end if
    end on
    on quit.pressed
        ui.stop()
    end on
end window

program main(args)
    g = { folder: args[0] }
    print to error "backend " + ui.backend()
    ui.open(app)
    ui.run()
end program
