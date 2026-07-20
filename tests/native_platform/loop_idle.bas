' NAP-3 (WI-4): gi.idle schedules a handler that the loop runs when otherwise idle.
' Same contract as timeout — returning false removes the source. Deterministic: the
' handler counts to 3 and quits, so the print order is fixed regardless of timing.
load gi
gi.require("GLib", "2.0")

s = {}
s.n = 0

function on_idle()
    s.n = s.n + 1
    print "idle " + s.n
    if s.n >= 3 then
        gi.quit()
        return false
    end if
    return true
end function

gi.idle(on_idle)
gi.main()
print "idle done"
