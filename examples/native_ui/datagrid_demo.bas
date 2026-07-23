' DataGrid demo — a virtualized table over the native rowmodel adapter.
' Two grids in tabs: a 1,000,000-row VIRTUAL source (rows computed on demand,
' nothing materialized) and a modest ARRAY-BACKED source (an array of records).
' Run under a display: GBASIC_PATH=stdlib gbasic examples/native_ui/datagrid_demo.bas
'
' NOTE (see docs/array_cow_design.md / DOGFOOD): reference each grid's factories
' from program scope before showing the window so on-screen binding is reliable
' in the current gi build — real apps that hold their widgets do this naturally.
load gi
load datagrid

gi.require("Gtk", "4.0")

_DATAGRID = datagrid.new_registry()

function row_count()
    return 1000000
end function

function row_cell(row, col)
    if col = 0 then return row
    if col = 1 then return "item-" + string(row)
    return row * 3
end function

function usd(v)
    return "$" + string(v)
end function

function build(app)
    ' --- virtual 1,000,000-row grid ---------------------------------------
    big = datagrid.create_virtual(row_count, row_cell)
    datagrid.add_column(big, { title: "#", index: 0 })
    datagrid.add_column(big, { title: "Name", index: 1 })
    datagrid.add_column(big, { title: "Amount", index: 2, format: usd })

    ' --- array-backed grid ------------------------------------------------
    accounts = [ { name: "Alice", balance: 1200 }, { name: "Bob", balance: 340 }, { name: "Carol", balance: 9999 } ]
    small = datagrid.create(accounts)
    datagrid.add_column(small, { title: "Account", field: "name" })
    datagrid.add_column(small, { title: "Balance", field: "balance", format: usd })

    ' hold factory references from program scope (see note above)
    _DATAGRID.big_id = big.id
    _DATAGRID.small_id = small.id

    notebook = gi.new("Gtk.Notebook")
    bigscroll = gi.new("Gtk.ScrolledWindow")
    bigscroll.set_child(datagrid.widget(big))
    smallscroll = gi.new("Gtk.ScrolledWindow")
    smallscroll.set_child(datagrid.widget(small))
    notebook.append_page(bigscroll, gi.new("Gtk.Label", "label", "1,000,000 rows"))
    notebook.append_page(smallscroll, gi.new("Gtk.Label", "label", "Accounts"))

    win = gi.new("Gtk.ApplicationWindow", "application", app)
    win.set_title("gBASIC DataGrid")
    win.set_default_size(560, 420)
    win.set_child(notebook)
    win.present()
end function

app = gi.new("Gtk.Application")
gi.connect(app, "activate", build)
app.run(0, nothing)
