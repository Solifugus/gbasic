' NAP-12 DataGrid display smoke — a REAL GtkColumnView on screen over the native
' rowmodel adapter, driven briefly under a display with G_DEBUG=fatal-criticals.
' It proves the on-screen path constructs, presents, and tears down cleanly with
' no GTK criticals, and that the model serves only a bounded number of rows for a
' 1,000,000-row logical source. Exact realized-row counts depend on the
' compositor and window size, so the asserted values are bounds (always true),
' not raw counts; deterministic data correctness is covered by logic.bas.
load gi
load datagrid
gi.require("Gtk", "4.0")
gi.invoke("Gtk.init")

_DATAGRID = datagrid.new_registry()

function _stop()
    gi.quit()
end function

function vcount()
    return 1000000
end function
function vcell(row, col)
    if col = 0 then return row
    return "row-" + string(row)
end function

vg = datagrid.create_virtual(vcount, vcell)
datagrid.add_column(vg, { title: "Index", index: 0 })
datagrid.add_column(vg, { title: "Label", index: 1 })

' Nothing here holds a factory: the grid's factories stay internal to the registry
' and rendering must work anyway (tests/datagrid/lifetime.bas is the dedicated
' regression for that).
win = gi.new("Gtk.Window")
win.set_default_size(500, 350)
sc = gi.new("Gtk.ScrolledWindow")
sc.set_child(datagrid.widget(vg))
win.set_child(sc)
win.present()

' Read the counters AFTER the widget tree is built: GtkColumnView realizes and
' binds its visible rows when the view is first given a size, not on present and
' not when the loop runs. Resetting here would zero work already done and make a
' healthy grid look inert.
mdl = datagrid.model(vg)
gi.timeout(500, _stop)
gi.main()

req = rowmodel.item_requests(mdl)
print "row_count: " + string(datagrid.row_count(vg))
print "rows were realized: " + string(req > 0)
print "requests bounded (not proportional to 1e6): " + string(req < 5000)
print "cells were bound: " + string(datagrid.accesses() > 0)
print "cell(0,1): " + datagrid.cell(vg, 0, 1)
print "cell(900000,1): " + datagrid.cell(vg, 900000, 1)
print "selected: " + string(datagrid.selected(vg))
datagrid.set_count(vg, 250000)
print "after set_count: " + string(datagrid.row_count(vg))
print "DONE"
