' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Increment 4 in a real terminal (tests/run_ui_tui.sh, tier live): a press
' on a table's row and a menu opened by F10, through the bytes a terminal
' sends. Events go to standard error; Quit ends the run.
window app( title: "Live" )
    menu "File"
        item hello( label: "Hello" )
        item quit( label: "Quit" )
    end menu
    table orders to show rows
        field who( label: "Who" )
    end table
    on orders.activated
        print to error "activated " + ui.cell(app, "orders", ui.selected(app, "orders"), "who")
    end on
    on hello.pressed
        print to error "hello pressed"
    end on
    on quit.pressed
        print to error "quit pressed"
        ui.stop()
    end on
end window

program main(args)
    rows = [{ who: "Ann" }, { who: "Bo" }, { who: "Cy" }]
    print to error "backend " + ui.backend()
    ui.open(app)
    ui.run()
end program
