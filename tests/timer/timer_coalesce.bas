' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' COALESCE -- the load-bearing fixture. A handler slower than the interval is
' the ORDINARY failure, and a catch-up schedule answers it with a burst: after
' the stall the backlog drains back to back with no gap at all, which on the
' event loop is the unbounded-queue defect PLAT-HTTP shipped once and whose
' symptom was A HANG, NOT A FAILURE.
'
' THE ASSERTION IS WHAT HAPPENS AFTER THE STALL ENDS, because that is where the
' two schedules differ observably. Three ticks are handled with 0.25s of work
' apiece on a 0.05s timer; then the handler stops sleeping and the next three
' ticks report their own `skipped`.
'
'   coalescing  -- the next due time is computed from the DELIVERY, so the
'                  timer is back on schedule at once: 0, 0, 0.
'   catch-up    -- the backlog drains one per iteration, so `skipped` counts
'                  DOWN as it unwinds: 4, 3, 2.
'
' A fixture that only counted ticks cannot tell them apart, because it stops at
' a fixed count either way.

G = { n: 0, skipped: 0, after: "", t: nothing }
G.t = timer.every(0.05)

watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        G.n = G.n + 1
        G.skipped = G.skipped + ev.skipped
        if G.n <= 3 then
            ' the stall: work LONGER than the interval
            sleep(0.25)
        else
            G.after = G.after + string(ev.skipped) + ","
        end if
        if G.n >= 7 then
            c = timer.cancel(G.t)
            print "DELIVERED " + string(G.n) + " SKIPPED " + string(G.skipped)
            print "AFTER " + G.after
        end if
    end while
end watch
