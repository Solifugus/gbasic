' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A one-shot removes itself, so the loop runs out of work and THE PROGRAM ENDS
' BY ITSELF -- the property that keeps `watch(timer.ticks)` from being a
' commitment to run forever. The runner's control is timer_forever.bas, which
' must NOT end, or "it exited" would be true of a build where timers never
' worked at all.
o = timer.after(0.2)
watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        print "FIRED count=" + string(ev.count) + " repeating=" + string(ev.timer.repeating)
    end while
end watch
