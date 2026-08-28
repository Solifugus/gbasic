' A table of rows. `datagrid` sits on a native row model, so a grid of a
' million rows realizes only the cells GTK actually asks for -- the point of
' the library is that virtualization, not the widget.
'
' A REGISTRY holds the callbacks and keeps them alive; without it a factory
' would be collected while GTK still holds a pointer to it. Assign it to
' `_DATAGRID` before creating anything.
load gi
load gtk
load datagrid

gtk.require()
gtk.init()

_DATAGRID = datagrid.new_registry()

rows = [
    { name: "ari.bas",   size: 41230 },
    { name: "chart.bas", size: 88104 },
    { name: "web.bas",   size: 52377 }
]

' EVERY datagrid update RETURNS THE NEW HANDLE and must be reassigned. gBASIC
' functions cannot change their caller, so an API that updates something hands
' back the updated value -- and the unused-result warning tells you when you
' forgot. `h = datagrid.add_column(h, ...)`, never a bare call.
h = datagrid.create(rows)
h = datagrid.add_column(h, { title: "File", field: "name" })
h = datagrid.add_column(h, { title: "Bytes", field: "size" })

print "rows            : " + string(datagrid.row_count(h))
print "cell 0,0        : " + datagrid.cell(h, 0, 0)
print "cell 1,1        : " + datagrid.cell(h, 1, 1)
print "widget type     : " + gi.type_name(datagrid.widget(h))

' The rows are a VALUE. Replacing them refreshes what the grid shows, and the
' original array is untouched -- gBASIC arrays are copy-on-write.
h = datagrid.set_rows(h, [{ name: "one.bas", size: 1 }])
print "after set_rows  : " + string(datagrid.row_count(h)) + " row, cell 0,0 = " + datagrid.cell(h, 0, 0)
print "original intact : " + string(count(rows)) + " rows"

h = datagrid.destroy(h)
print "destroyed       : " + string(datagrid.destroyed(h))
