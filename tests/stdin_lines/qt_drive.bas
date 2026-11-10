' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' A window driven by lines on standard input while qt.run() holds the thread
' (Qt design §0.38 D3): "set <text>" puts text in a label and prints what Qt
' then holds, "ticks" prints how many times a 0.1-second timer has fired
' meanwhile, "quit" stops Qt's loop. The runner's driver waits between lines,
' so ticks only count up if the loop kept running while a line was awaited.
window w( title: "Driven", width: 300, height: 120 )
    column
        text shown( label: "nothing yet" )
    end column
end window

program main(args)
    g = { ticks: 0 }
    if not qt.can_display() then
        print "NODISPLAY"
        return
    end if
    qt.open(w)
    t = timer.every(0.1)
    watch tk(timer.ticks)
        while count(timer.ticks) > 0
            x = take_first(timer.ticks)
            g.ticks = g.ticks + 1
        end while
    end watch
    watch lines(stdin.lines)
        while count(stdin.lines) > 0
            l = take_first(stdin.lines)
            if starts_with(l, "set ") then
                qt.set(w, "shown", "text", mid(l, 4, len(l) - 4))
                print "shown " + qt.read(w, "shown", "text")
            else if l = "ticks" then
                print "ticks " + string(g.ticks)
            else if l = "quit" then
                qt.stop()
            end if
        end while
    end watch
    qt.run()
    print "run returned"
    unwatch tk
    unwatch lines
    x = timer.cancel(t)
end program
