' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A window that cannot do without `docking` (tests/run_ui_tui.sh, tier
' needs): run where only the terminal can draw, no backend is chosen and the
' first ui verb says why.
window desk( title: "Desk", needs: ["docking"] )
    column
        text t( label: "hello" )
    end column
end window

program main(args)
    print to error "backend " + string(ui.backend())
    ui.open(desk)
end program
