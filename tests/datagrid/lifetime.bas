' NAP-12 follow-up — DataGrid factory/callback LIFETIME.
'
' The primary regression: a grid whose factories are reachable ONLY through the
' datagrid registry must render. Nothing here holds a factory, column, view, or
' model in a variable of its own — the grids are even built inside a helper that
' RETURNS before they are used — and every grid must still fire setup/bind and
' show correct cell text.
'
' Counters are read at the points where the work actually happens: GtkColumnView
' realizes and binds its visible rows when the view is first given a size (added
' to a window), not on present and not when the loop runs. Exact counts depend on
' window size and compositor, so this asserts booleans and relations, never raw
' counts.
load gi
load datagrid
gi.require("Gtk", "4.0")
gi.invoke("Gtk.init")

_DATAGRID = datagrid.new_registry()

function stopit()
    gi.quit()
    return false
end function

function pump(ms)
    gi.timeout(ms, stopit)
    gi.main()
end function

function usd(v)
    return "$" + string(v)
end function

function vcount()
    return 100000
end function
function vcell(row, col)
    if col = 0 then return row
    return "v" + string(row)
end function

' Builds a grid and returns ONLY the handle: the factories, columns, view and
' model all go out of scope here. This is the failing form from the NAP-12 report.
function make_accounts(rows)
    h = datagrid.create(rows)
    datagrid.add_column(h, { title: "Account", field: "name" })
    datagrid.add_column(h, { title: "Balance", field: "balance", format: usd })
    return h
end function

' Shows a grid in its own window; the window is returned, nothing else is kept.
function show(h, title)
    w = gi.new("Gtk.Window")
    w.set_default_size(420, 300)
    s = gi.new("Gtk.ScrolledWindow")
    s.set_child(datagrid.widget(h))
    w.set_child(s)
    w.set_title(title)
    w.present()
    return w
end function

' Unrelated record/array traffic, to pressure any premature release.
function churn()
    junk = []
    i = 0
    while i < 300
        junk = append(junk, { a: i, b: "x" + string(i), c: [i, i + 1] })
        i = i + 1
    end while
    return count(junk)
end function

rows = []
i = 0
while i < 400
    rows = append(rows, { name: "acct-" + string(i), balance: i * 7 })
    i = i + 1
end while

' --- primary: no external retention ---------------------------------------
datagrid.reset_accesses()
h = make_accounts(rows)
churned = churn()
built_binds = datagrid.accesses()
print "no binds before the view is sized: " + string(built_binds = 0)

wh = show(h, "Accounts")
setups_after_embed = datagrid.setups()
binds_after_embed = datagrid.accesses()
print "setup fired without external retention: " + string(setups_after_embed > 0)
print "bind fired without external retention: " + string(binds_after_embed > 0)
pump(250)

' Multiple columns bind the correct field each.
print "cell(0,0)=" + datagrid.cell(h, 0, 0) + " cell(0,1)=" + datagrid.cell(h, 0, 1)
print "cell(3,0)=" + datagrid.cell(h, 3, 0) + " cell(3,1)=" + datagrid.cell(h, 3, 1)

' --- lifetime pressure: churn, then refresh must still bind ----------------
churned2 = churn()
before_refresh = datagrid.accesses()
datagrid.refresh(h)
pump(250)
after_refresh = datagrid.accesses()
print "still binds after churn + refresh: " + string(after_refresh > before_refresh)

' --- no duplicate handlers: per-cycle bind count stays flat ---------------
cycle_counts = []
n = 0
while n < 3
    b0 = datagrid.accesses()
    datagrid.refresh(h)
    pump(200)
    b1 = datagrid.accesses()
    cycle_counts = append(cycle_counts, b1 - b0)
    n = n + 1
end while
flat = true
for each c in cycle_counts
    first = cycle_counts[0]
    if c != first then flat = false
end for
print "refresh cycles bind a constant amount (no duplicate handlers): " + string(flat)

' --- two independent grids -------------------------------------------------
g2 = datagrid.create_virtual(vcount, vcell)
datagrid.add_column(g2, { title: "Idx", index: 0 })
datagrid.add_column(g2, { title: "Label", index: 1 })
w2 = show(g2, "Virtual")
pump(250)
print "grid2 binds independently: " + string(datagrid.setups() > setups_after_embed)
print "grid1 cell(2,0)=" + datagrid.cell(h, 2, 0) + " grid2 cell(2,1)=" + datagrid.cell(g2, 2, 1)

' Destroying the first window must not disturb the second.
wh.destroy()
datagrid.destroy(h)
pump(200)
b_before = datagrid.accesses()
datagrid.refresh(g2)
pump(250)
print "grid2 still binds after grid1 destroyed: " + string(datagrid.accesses() > b_before)
print "grid2 cell(9,1)=" + datagrid.cell(g2, 9, 1)
print "grid1 reports destroyed: " + string(datagrid.destroyed(h))

' --- repeated create/destroy must not accumulate --------------------------
before_slots = count(_DATAGRID.grids)
k = 0
while k < 5
    t = datagrid.create_virtual(vcount, vcell)
    datagrid.add_column(t, { title: "T", index: 0 })
    tw = show(t, "T")
    pump(120)
    tw.destroy()
    datagrid.destroy(t)
    k = k + 1
end while
after_slots = count(_DATAGRID.grids)
live = 0
for each g in _DATAGRID.grids
    if g.kind != "destroyed" then live = live + 1
end for
print "create/destroy cycles retain no live grids beyond the survivor: " + string(live = 1)
print "slots grew by exactly the cycle count: " + string(after_slots - before_slots = 5)
print "DONE"
