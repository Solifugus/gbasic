' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' THE CONTROL for timer_oneshot.bas: a repeating timer keeps the loop alive, so
' this program does NOT end on its own and the runner has to kill it.
t = timer.every(0.1)
watch(timer.ticks)
    while count(timer.ticks) > 0
        ev = take_first(timer.ticks)
        print "TICK " + string(ev.count)
    end while
end watch
