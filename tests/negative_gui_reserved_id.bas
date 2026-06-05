load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"_root", component:"label", value:"Reserved" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
