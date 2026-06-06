load "gui"

ui = {
    id:"main",
    component:"vert",
    spacing:"between",
    contains:[
        { id:"name", component:"input", value:"Ada" },
        { id:"save", component:"button", label:"Save", value:false },
        { id:"status", component:"label", value:"Ready" }
    ]
}

win = gui.window(400, 300, "Watcher Demo", ui)

watch win.name.value
    print "name committed"
    print win.name.value
    win.status.value = "Committed: " + win.name.value
end watch

watch win.save.value
    if win.save.value then
        print "save clicked"
        win.status.value = "Saving..."
        win.save.label = "Working..."
        win.status.value = "Saved via watcher"
        win.save.label = "Save"
        win.save.value = false
    end if
end watch

gui.run(win)
print win.status.value
