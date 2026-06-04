load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"mystery", component:"grid", value:"?" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
