' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The terminal's events delivered by the event loop (tests/run_term.sh, tier
' watch), beside a 0.1-second timer that must keep firing while no key comes.
' Each event goes to standard error as one line; "q" closes the terminal and
' stops the timer, so the loop has nothing left and the program ends.
load term

program main(args)
    g = { ticks: 0 }
    term.open()
    print to error "ready"
    t = timer.every(0.1)
    watch tk(timer.ticks)
        while count(timer.ticks) > 0
            x = take_first(timer.ticks)
            g.ticks = g.ticks + 1
        end while
    end watch
    watch keys(term.events)
        while count(term.events) > 0
            e = take_first(term.events)
            if e.kind = "key" then
                print to error "key " + e.key + " ticks " + string(g.ticks)
                if e.key = "w" then
                    on error goto next
                    x = term.wait(0)
                    if error then
                        print to error "wait refused: " + string(starts_with(error.message, "term.wait: term.events is watched"))
                    end if
                end if
                if e.key = "q" then
                    term.close()
                    x = timer.cancel(t)
                    unwatch keys
                    unwatch tk
                end if
            end if
        end while
    end watch
end program
