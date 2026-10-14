' THE RENDERER SEAM, driven by a backend that is not GTK.
'
' WHY THIS FIXTURE IS THE LOAD-BEARING ONE: a seam with a single implementation
' is indistinguishable from no seam. The reconciler could be refactored to call
' `b.create` instead of `gi.new` and still be welded to GTK in a dozen hidden
' ways, and every existing tier -- headless logic and the GTK display smoke --
' would stay green. So this file drives `gtkui` end to end over a RECORDING
' backend written here in the fixture: no `load gi`, no typelib, no display, no
' widget. If the reconciler still needed GTK for anything, this could not run.
'
' IT ALSO ASSERTS THE RECONCILER'S OWN PROMISES, which the display tier can only
' assert through real widgets: that a property-only change REUSES the instance,
' that a type change REPLACES it, that keyed children are inserted, removed and
' reordered rather than rebuilt, and that signals are connected once.
'
' A WIDGET HERE IS AN INTEGER ID into a program-global store, because a gBASIC
' record is a VALUE -- a backend that handed back records could not mutate what
' the reconciler holds. That is the same reason `fake.bas`'s generators are pure
' functions of (seed, index), and it is what a non-object backend (an SVG
' document, a remote surface) would have to do anyway.

load gtkui

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' ---- the recording backend ------------------------------------------------

function rb_kind(t)
    if t = "box" then return "box"
    if t = "slot" then return "single"
    return "leaf"
end function

function rb_create(t)
    id = count(g_w)
    append(g_w, { type: t, props: {}, kids: [], live: 0 })
    append(g_log, "create " + t + " #" + string(id))
    return id
end function

function rb_set_prop(w, name, value)
    g_w[w].props[name] = value
    append(g_log, "set #" + string(w) + " " + name + "=" + string(value))
    return nothing
end function

function rb_connect(w, signal, handler)
    g_w[w].live = g_w[w].live + 1
    append(g_log, "connect #" + string(w) + " " + signal)
    return "h" + string(w) + ":" + signal
end function

function rb_disconnect(w, id)
    g_w[w].live = g_w[w].live - 1
    append(g_log, "disconnect #" + string(w) + " " + string(id))
    return nothing
end function

function rb_attach(c, kindname, child, childkey)
    if kindname = "single" then
        g_w[c].kids = [ child ]
    else
        append(g_w[c].kids, child)
    end if
    append(g_log, "attach #" + string(child) + " -> #" + string(c) + " (" + kindname + ")")
    return nothing
end function

function rb_detach(c, kindname, child, childkey)
    if kindname = "single" then
        g_w[c].kids = []
    else
        g_w[c].kids = remove_value(g_w[c].kids, child)
    end if
    append(g_log, "detach #" + string(child) + " <- #" + string(c) + " (" + kindname + ")")
    return nothing
end function

function rb_reorder_after(c, child, prev)
    kids = remove_value(g_w[c].kids, child)
    if prev = nothing then
        g_w[c].kids = concat([ child ], kids)
    else
        at = find(kids, prev)
        g_w[c].kids = concat(concat(slice(kids, 0, at + 1), [ child ]), slice(kids, at + 1, count(kids) - at - 1))
    end if
    append(g_log, "reorder #" + string(child) + " after " + string(prev))
    return nothing
end function

' THE ROOT SLOT IS A ONE-ELEMENT ARRAY, and that is not stylistic. The first
' version of these two assigned a program global directly (`g_root = child`), and
' gBASIC HAS NO CLOSURES: a function that assigns an enclosing name gets a silent
' function-local, so the root was never recorded. gBASIC is right to say nothing
' -- the warning fires only when the name is READ first, and these never read it
' -- which is exactly why only an assertion could catch it. It cost two checks:
' "the root was attached" failed honestly, and "the root slot is empty" PASSED
' VACUOUSLY, because the slot had never been filled. An index write resolves the
' global and writes through.
function rb_parent_attach(p, child)
    g_rootslot[0] = child
    append(g_log, "root <- #" + string(child))
    return nothing
end function

function rb_parent_detach(p, child)
    g_rootslot[0] = nothing
    append(g_log, "root detach #" + string(child))
    return nothing
end function

function rb_describe(w)
    return "#" + string(w) + " " + g_w[w].type
end function

function rb_backend()
    b = {}
    b.name = "record"
    b.kind = rb_kind
    b.create = rb_create
    b.set_prop = rb_set_prop
    b.connect = rb_connect
    b.disconnect = rb_disconnect
    b.attach = rb_attach
    b.detach = rb_detach
    b.reorder_after = rb_reorder_after
    b.parent_attach = rb_parent_attach
    b.parent_detach = rb_parent_detach
    b.describe = rb_describe
    return b
end function

' ---- rendering the shadow tree, so order is assertable --------------------

function render(id)
    out = g_w[id].type
    if has(g_w[id].props, "label") then
        out = out + ":" + string(g_w[id].props["label"])
    end if
    if count(g_w[id].kids) > 0 then
        inner = ""
        for each k in g_w[id].kids
            if inner != "" then
                inner = inner + ","
            end if
            inner = inner + render(k)
        end for
        out = out + "[" + inner + "]"
    end if
    return out
end function

function onclick(w)
    return nothing
end function

program main( args )
    g_w = []
    g_log = []
    g_rootslot = [ nothing ]

    b = rb_backend()
    parent = 0 - 1          ' a foreign parent the backend never made

    print "-- the reconciler runs with NO GTK: no `load gi`, no typelib, no display"
    d1 = { type: "box", children: [
             { type: "cell", key: "a", props: { label: "A" }, signals: { clicked: onclick } },
             { type: "cell", key: "b", props: { label: "B" } }
           ] }
    h = gtkui.mount(b, parent, d1)
    check("the tree is built        ", render(gtkui.root(h)), "box[cell:A,cell:B]")
    check("and the root was attached", g_rootslot[0], gtkui.root(h))
    check("one signal was connected ", g_w[gtkui.lookup(h, "a")].live, 1)

    print "-- a property-only change REUSES the instance"
    was = gtkui.lookup(h, "a")
    d2 = { type: "box", children: [
             { type: "cell", key: "a", props: { label: "A2" }, signals: { clicked: onclick } },
             { type: "cell", key: "b", props: { label: "B" } }
           ] }
    h = gtkui.update(h, d2)
    check("the label changed        ", render(gtkui.root(h)), "box[cell:A2,cell:B]")
    check("the SAME widget was kept ", gtkui.lookup(h, "a"), was)
    check("and the signal is not dou", g_w[was].live, 1)

    print "-- a keyed insert, and the order it asks for"
    d3 = { type: "box", children: [
             { type: "cell", key: "b", props: { label: "B" } },
             { type: "cell", key: "c", props: { label: "C" } },
             { type: "cell", key: "a", props: { label: "A2" }, signals: { clicked: onclick } }
           ] }
    h = gtkui.update(h, d3)
    check("reordered with a new kid ", render(gtkui.root(h)), "box[cell:B,cell:C,cell:A2]")
    check("`a` is still the same one", gtkui.lookup(h, "a"), was)

    print "-- a key that disappears is detached and its handlers released"
    d4 = { type: "box", children: [
             { type: "cell", key: "b", props: { label: "B" } }
           ] }
    h = gtkui.update(h, d4)
    check("only `b` remains         ", render(gtkui.root(h)), "box[cell:B]")
    check("`a` is gone from the tree", gtkui.lookup(h, "a"), nothing)
    check("and its signal released  ", g_w[was].live, 0)

    print "-- a type change REPLACES rather than reuses"
    oldroot = gtkui.root(h)
    d5 = { type: "slot", children: [ { type: "cell", props: { label: "only" } } ] }
    h = gtkui.update(h, d5)
    check("a new root was built     ", render(gtkui.root(h)), "slot[cell:only]")
    check("and it is a NEW instance ", gtkui.root(h) != oldroot, true)

    print "-- unmount detaches the root"
    gtkui.unmount(h)
    check("the root slot is empty   ", g_rootslot[0], nothing)

    print "-- the seam was exercised, not bypassed"
    kinds = {}
    for each entry in g_log
        verb = split(entry, " ")[0]
        kinds[verb] = true
    end for
    for each want in ["create", "set", "connect", "disconnect", "attach", "detach", "reorder", "root"]
        check("the backend saw `" + want + "`", has(kinds, want), true)
    end for
    check("nothing GTK was needed   ", b.name, "record")
end program
