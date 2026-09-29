' A widget field that is not a field. The validator used to check the ones it
' knew and IGNORE the rest, so this rendered a widget with no value and said
' nothing -- a typo indistinguishable from the feature working.
'
' Its CONTROL is examples/gui_fields_test.bas, which uses every known field
' including `spacing` and must still be ACCEPTED. Without that, "unknown fields
' are refused" is satisfied by a validator that refuses everything.
load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"greeting", component:"label", valeu:"hello" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
