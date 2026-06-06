load "gui"

ui = {
    id:"main",
    component:"vert",
    spacing:"wide",
    contains:[
        { id:"status", component:"label", value:"Ready" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
