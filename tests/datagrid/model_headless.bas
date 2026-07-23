' NAP-12 native row-model — headless proof (no display, no GTK; Gio only). The
' GbRowModel adapter (rowmodel.*) is a data-free GListModel: it serves a row
' count and lazy index-carrying proxies, and materializes NO per-row data. This
' verifies count, deep get_item, item identity, change notification, and — the
' virtualization proof — that only the rows actually requested are ever touched,
' independent of the logical row count.

' ---- a 1,000,000-row logical model, zero row data materialized -------------
m = rowmodel.new(42)
rowmodel.set_count(m, 1000000)
print "count: " + string(rowmodel.count(m))

' deep lazy access: the row proxy carries its index and grid id, nothing else
deep = rowmodel.get_item(m, 900000)
print "deep is_row: " + string(rowmodel.is_row(deep))
print "deep index: " + string(rowmodel.row_index(deep))
print "deep grid: " + string(rowmodel.row_grid(deep))

' out-of-range yields nothing (GListModel contract)
oob = rowmodel.get_item(m, 1000000)
print "oob is nothing: " + string(oob = nothing)

' ---- virtualization proof: requests are bounded by what's asked for, not by
'      the millon-row logical size ------------------------------------------
rowmodel.reset_requests(m)
k = 0
acc = 0
while k < 300
    pos = k * 3000
    r = rowmodel.get_item(m, pos)
    acc = acc + rowmodel.row_index(r)
    k = k + 1
end while
print "requests after 300 gets: " + string(rowmodel.item_requests(m))
cnt = rowmodel.count(m)
print "count still 1,000,000: " + string(cnt = 1000000)
print "index sum: " + string(acc)

' ---- change notification adjusts the count coherently ----------------------
rowmodel.items_changed(m, 100, 50, 70)
print "after splice count: " + string(rowmodel.count(m))
rowmodel.set_count(m, 5)
print "after set_count: " + string(rowmodel.count(m))
