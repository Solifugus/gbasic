load "gui"

ui = {
    id:"main",
    component:"vert",
    spacing:8,
    contains:[
        { id:"name", component:"input", value:"Ada" },
        { id:"save", component:"button", label:"Save", value:true },
        { id:"status", component:"label", value:"Starting" }
    ]
}

win = gui.window(400, 300, "Access Demo", ui)
win.status.value = "Ready"
win.save.label = "Commit"
win.save.value = false
print(win.name.value)
gui.run(win)
print(win.name.value)
print(win.save.value)
