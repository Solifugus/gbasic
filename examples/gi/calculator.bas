' ============================================================================
' calculator.bas — a working GTK4 calculator on the raw gi.* bridge.
'
' Exercises more of the bridge than gtk4_hello: Gtk.Grid with multi-int-argument
' method calls (grid.attach child,col,row,w,h), a Gtk.Entry display, string/float/
' boolean property sets, gi.enum, and one shared "clicked" handler wired to all 16
' buttons that reads each button's own label to decide what to do.
'
' STATE lives in the widgets, not in gBASIC variables: assigning a gBASIC global
' inside a function only shadows it locally, so the running expression is kept in
' the Gtk.Entry's "text" property and read/written through gi.get/gi.set.
'
' Arithmetic is evaluated strictly LEFT-TO-RIGHT (no operator precedence), the way
' a simple pocket calculator works: 2+3*4 = 20, not 14.
'
' RUN IT (needs GTK4 runtime + typelib + a display):
'   sudo apt-get install gir1.2-gtk-4.0 libgtk-4-1
'   ./gbasic examples/gi/calculator.bas
'
' Manual demo (needs a display); not part of the golden suite.
' ============================================================================

load gi
gi.require("Gio", "2.0")
gi.require("Gtk", "4.0")

' --- expression evaluation (pure gBASIC, headless-testable) -----------------

' Apply one pending operator to the running result and the just-parsed number.
' op = "" means `acc` is the very first number, so it seeds the result.
function apply_op(result, op, acc)
    if acc = "" then
        return result
    end if
    v = number(acc)
    if op = "" then
        return v
    end if
    if op = "+" then
        return result + v
    end if
    if op = "-" then
        return result - v
    end if
    if op = "*" then
        return result * v
    end if
    if op = "/" then
        if v = 0 then
            return result
        end if
        return result / v
    end if
    return result
end function

' Evaluate a flat expression string left-to-right. Note: gBASIC `mid` is 0-based
' in its start position, and strings are walked with mid(s, i, 1).
function eval_expr(expr)
    result = 0
    acc = ""
    op = ""
    i = 0
    n = len(expr)
    while i < n
        ch = mid(expr, i, 1)
        if ch = "+" or ch = "-" or ch = "*" or ch = "/" then
            result = apply_op(result, op, acc)
            op = ch
            acc = ""
        else
            acc = acc + ch
        end if
        i = i + 1
    end while
    result = apply_op(result, op, acc)
    return string(result)
end function

' --- application + window (register triggers gtk_init via startup) -----------

app = gi.new("Gtk.Application")
gi.set(app, "application-id", "org.gbasic.Calculator")
gi.call(app, "register", nothing)

win = gi.new("Gtk.Window")
gi.call(app, "add_window", win)
gi.set(win, "title", "gBASIC Calculator")
gi.set(win, "default-width", 260)
gi.set(win, "default-height", 320)

' --- display: a read-only, right-aligned Gtk.Entry holds all the state --------

display = gi.new("Gtk.Entry")
gi.set(display, "editable", false)
gi.set(display, "text", "")
gi.set(display, "xalign", 1.0)
gi.set(display, "hexpand", true)

' --- layout: a vertical box with the display over a homogeneous button grid ---

root = gi.new("Gtk.Box")
gi.set(root, "orientation", gi.enum("Gtk.Orientation.VERTICAL"))
gi.set(root, "spacing", 6)
gi.set(root, "margin-top", 10)
gi.set(root, "margin-bottom", 10)
gi.set(root, "margin-start", 10)
gi.set(root, "margin-end", 10)
gi.call(root, "append", display)

grid = gi.new("Gtk.Grid")
gi.set(grid, "row-spacing", 6)
gi.set(grid, "column-spacing", 6)
gi.set(grid, "row-homogeneous", true)
gi.set(grid, "column-homogeneous", true)
gi.set(grid, "hexpand", true)
gi.set(grid, "vexpand", true)
gi.call(root, "append", grid)

gi.call(win, "set_child", root)

' --- one handler for every button; it reads its own label --------------------

function on_click(source)
    key = gi.get(source, "label")
    if key = "C" then
        gi.set(display, "text", "")
    else
        if key = "=" then
            gi.set(display, "text", eval_expr(gi.get(display, "text")))
        else
            gi.set(display, "text", gi.get(display, "text") + key)
        end if
    end if
end function

' Create a button, place it in the grid, and wire its click to on_click.
function add_key(text, col, row)
    b = gi.new("Gtk.Button")
    gi.set(b, "label", text)
    gi.set(b, "hexpand", true)
    gi.set(b, "vexpand", true)
    gi.call(grid, "attach", b, col, row, 1, 1)
    gi.connect(b, "clicked", on_click)
end function

' Standard 4-column keypad.
add_key("7", 0, 0)
add_key("8", 1, 0)
add_key("9", 2, 0)
add_key("/", 3, 0)

add_key("4", 0, 1)
add_key("5", 1, 1)
add_key("6", 2, 1)
add_key("*", 3, 1)

add_key("1", 0, 2)
add_key("2", 1, 2)
add_key("3", 2, 2)
add_key("-", 3, 2)

add_key("C", 0, 3)
add_key("0", 1, 3)
add_key("=", 2, 3)
add_key("+", 3, 3)

' --- close ends the loop; show and run --------------------------------------

function on_close(source)
    gi.quit()
end function

gi.connect(win, "close-request", on_close)
gi.call(win, "present")
gi.main()
