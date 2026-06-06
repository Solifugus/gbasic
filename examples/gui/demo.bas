load "gui"

ui = {
    id:"main",
    component:"vert",
    spacing:"between",
    contains:[
        { id:"title", component:"label", value:"Hello gBASIC GUI" },
        { id:"name", component:"input", value:"" },
        { id:"save", component:"button", label:"Save", value:false },
        { id:"status", component:"label", value:"Ready" }
    ]
}

win = gui.window(400, 300, "Demo", ui)
win.status.value = "Changed before run"
win.save.label = "Commit"
win.save.value = false
print(win.name.value)
gui.run(win)
