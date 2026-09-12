' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' CADENCE: ten 0.1s ticks. The fixture reports WHAT IT SAW and nothing about
' time, because gBASIC's clock is second-resolution (`epoch(now())` cannot see
' a 0.25s sleep at all) -- so the runner times this from OUTSIDE against the
' wall clock, which is the same standard run_core.sh holds `sleep` to and a
' stronger oracle than anything the program could say about itself.

G = { n: 0, skipped: 0, t: nothing }
G.t = timer.every(0.1)

watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        G.n = G.n + 1
        G.skipped = G.skipped + ev.skipped
        if G.n >= 10 then
            c = timer.cancel(G.t)
            print "DELIVERED " + string(G.n) + " SKIPPED " + string(G.skipped)
        end if
    end while
end watch
