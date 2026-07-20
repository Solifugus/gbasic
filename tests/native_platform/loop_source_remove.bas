' NAP-3 (WI-4): gi.source_remove cancels a scheduled source by id. A long-delay
' "doomed" timeout is removed before it can fire; a short "keeper" timeout runs
' twice and quits. "doomed fired" must never appear — proving the removal took.
load gi
gi.require("GLib", "2.0")

s = {}
s.n = 0

function keeper()
    s.n = s.n + 1
    print "keeper " + s.n
    if s.n >= 2 then
        gi.quit()
        return false
    end if
    return true
end function

function doomed()
    print "doomed fired"
    return true
end function

id = gi.timeout(10000, doomed)
gi.timeout(5, keeper)
gi.source_remove(id)
gi.main()
print "remove done"
