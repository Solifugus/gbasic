' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The tui backend's layout, drawn with no terminal and written as text by
' GBASIC_TUI_DUMP (tests/run_ui_tui.sh, tier layout): a title line, a group's
' box, a form's captions in a column of their own, a field with its caption
' beside it in a column, a size in characters, a separator, a row whose
' stretch pushes two buttons to the right, an icon-only button showing its
' name, and the caret in the focused field.
window shop( title: "Order" )
    group "Customer"
        form to edit cust
            field name( label: "Name" )
            field city( label: "City", hint: "where" )
        end form
    end group
    field note( label: "Note", min_width: 30 columns, max_width: 30 columns )
    check rush( label: "Rush" )
    separator()
    row
        button help( icon: "tests/gpdf/images/alpha.png" )
        stretch()
        button cancel( label: "Cancel" )
        button ok( label: "Place order", default: true )
    end row
end window

program main(args)
    cust = { name: "Ann Lee", city: "" }
    ui.open(shop)
    k = ui.key(shop, "note", "x")
end program
