' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A window in a real terminal, the backend chosen by the selection rule
' (tests/run_ui_tui.sh, tier live): with no display and a terminal, `tui`.
' Each event goes to standard error, which the driver points at a file.
' With the argument `close`, it waits for Ctrl-Q instead of the Go button.
window ask( title: "Ask" )
    column
        field who( label: "Who" )
        check sure( label: "Sure" )
        row
            stretch()
            button go( label: "Go", default: true )
        end row
    end column
    on go.pressed
        print to error "go: " + ui.read(ask, "who") + " sure=" + ui.read(ask, "sure")
        ui.stop()
    end on
    on ask.closed
        print to error "closed"
    end on
end window

program main(args)
    print to error "backend " + ui.backend()
    ui.open(ask)
    ui.run()
    print to error "after run, open " + string(ui.is_open(ask))
    print "printed after the run"
end program
