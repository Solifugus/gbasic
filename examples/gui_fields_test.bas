' THE CONTROL for tests/negative_gui_unknown_field.bas: every field the renderer
' reads, on one tree, must be ACCEPTED -- or "unknown fields are refused" is
' satisfied by a validator that refuses everything.
'
' `spacing` is the one that earns this file. It is implemented
' (gui_spacing_mode_for_record) and was NOT in the list the validator checked,
' so the two had drifted; a refusal built from the validator's own list would
' have broken the seven uses in examples/gui and nothing here would have caught
' it, because no gui program RUNS in this gate -- they are parse-only or
' display-gated. This one runs, headless: validation happens before any display
' is needed.
load gui

ui = {
    id: "root",
    component: "vert",
    spacing: "center",
    width: 320,
    height: 240,
    visible: true,
    enabled: true,
    contains: [
        { id: "greeting", component: "label", value: "hello" },
        { id: "name", component: "input", value: "" },
        { id: "go", component: "button", label: "Go", value: false },
        { id: "gap", component: "spacer" },
        { id: "row", component: "horiz", spacing: "end", contains: [
            { id: "cancel", component: "button", label: "Cancel", value: false }
        ] }
    ]
}

win = gui.window(320, 240, "Fields", ui)
print "every known widget field accepted"
print win.greeting.value
print win.cancel.label   ' FLAT: ids are unique per window, so not win.row.cancel
