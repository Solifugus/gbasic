load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"save-button", component:"button", label:"Save", value:false }
    ]
}

win = gui.window(400, 300, "Demo", ui)
