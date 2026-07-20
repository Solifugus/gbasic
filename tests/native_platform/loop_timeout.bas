' NAP-3 (WI-4): gi.timeout drives a GLib main loop headlessly. The handler runs on
' each tick; returning false removes the source, and gi.quit ends the loop so
' gi.main() returns. Handler state is carried in a top-level record (a function
' cannot rebind a top-level scalar, but mutating a shared record field persists).
load gi
gi.require("GLib", "2.0")

s = {}
s.ticks = 0

function on_tick()
    s.ticks = s.ticks + 1
    print "tick " + s.ticks
    if s.ticks >= 3 then
        gi.quit()
        return false
    end if
    return true
end function

gi.timeout(5, on_tick)
gi.main()
print "loop done"
