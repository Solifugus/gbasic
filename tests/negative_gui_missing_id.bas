load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { component:"label", value:"Missing id" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
