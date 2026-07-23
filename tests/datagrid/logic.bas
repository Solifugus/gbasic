' NAP-12 DataGrid logic — deterministic, no rendering. Builds real grids (needs
' Gtk.init) but never presents a window or pumps the loop, so every result is
' deterministic. datagrid.cell(handle, row, col) runs the exact _cell + _format
' path the on-screen bind handler uses, so this asserts displayed-value
' correctness for every source shape, plus COW snapshot semantics, selection
' default, refresh, and virtual deep-row access — all without a display frame.
load gi
load datagrid
gi.require("Gtk", "4.0")
gi.invoke("Gtk.init")

_DATAGRID = datagrid.new_registry()

function money(v)
    return "$" + string(v)
end function
function vcount()
    return 1000000
end function
function vcell(row, col)
    if col = 0 then return row
    return "row-" + string(row)
end function

' ---- array of records ----------------------------------------------------
recs = [ { name: "Alice", balance: 100 }, { name: "Bob", balance: 200 } ]
gr = datagrid.create(recs)
datagrid.add_column(gr, { title: "Name", field: "name" })
datagrid.add_column(gr, { title: "Balance", field: "balance" })
print "recs row_count: " + string(datagrid.row_count(gr))
print "recs cell(0,0): " + datagrid.cell(gr, 0, 0)
print "recs cell(0,1): " + datagrid.cell(gr, 0, 1)
print "recs cell(1,0): " + datagrid.cell(gr, 1, 0)
print "recs oob cell(5,0): [" + datagrid.cell(gr, 5, 0) + "]"

' ---- array of arrays -----------------------------------------------------
rowsarr = [ ["Alice", 100], ["Bob", 200] ]
ga = datagrid.create(rowsarr)
datagrid.add_column(ga, { title: "Name", index: 0 })
datagrid.add_column(ga, { title: "Bal", index: 1 })
print "arr cell(1,0): " + datagrid.cell(ga, 1, 0)
print "arr cell(1,1): " + datagrid.cell(ga, 1, 1)

' ---- scalar array --------------------------------------------------------
scal = [10, 20, 30]
gs = datagrid.create(scal)
datagrid.add_column(gs, { title: "N" })
print "scalar cell(2,0): " + datagrid.cell(gs, 2, 0)

' ---- formatter -----------------------------------------------------------
gf = datagrid.create(recs)
datagrid.add_column(gf, { title: "Bal", field: "balance", format: money })
print "formatted cell(1,0): " + datagrid.cell(gf, 1, 0)

' ---- virtual source, deep row --------------------------------------------
gv = datagrid.create_virtual(vcount, vcell)
datagrid.add_column(gv, { title: "Idx" })
datagrid.add_column(gv, { title: "Label" })
print "virtual row_count: " + string(datagrid.row_count(gv))
print "virtual cell(900000,0): " + datagrid.cell(gv, 900000, 0)
print "virtual cell(900000,1): " + datagrid.cell(gv, 900000, 1)

' ---- COW snapshot semantics ----------------------------------------------
src = [ { name: "orig" } ]
gc = datagrid.create(src)
datagrid.add_column(gc, { title: "Name", field: "name" })
src[0].name = "MUTATED"
print "snapshot (source mutation ignored): " + datagrid.cell(gc, 0, 0)
datagrid.set_rows(gc, [ { name: "replaced" }, { name: "second" } ])
print "after set_rows count: " + string(datagrid.row_count(gc))
print "after set_rows cell: " + datagrid.cell(gc, 0, 0)

' ---- selection default + refresh -----------------------------------------
print "default selected: " + string(datagrid.selected(gr))
datagrid.set_count(gv, 500000)
print "virtual after set_count: " + string(datagrid.row_count(gv))
