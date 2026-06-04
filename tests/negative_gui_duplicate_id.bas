load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"save", component:"button", label:"Save", value:false },
        { id:"save", component:"label", value:"Duplicate" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
