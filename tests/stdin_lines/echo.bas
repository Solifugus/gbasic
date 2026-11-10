' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Each line from standard input, as `watch(stdin.lines)` delivers it (Qt
' design §0.38 D3): echoed as its length and its hex, so a line holding any
' byte comes back unambiguous, then `closed` when input ends. The runner's
' python3 driver writes the input and checks every line it wrote came back,
' in order. The most lines any one firing found is printed too: 1, since
' each line is delivered on its own. (Firings themselves are not counted: the
' handler's own take_first changes the watched path, which fires it once more
' with nothing left, so the number of firings says nothing about delivery.)
program main(args)
    g = { most: 0 }
    watch w(stdin.lines)
        if count(stdin.lines) > g.most then
            g.most = count(stdin.lines)
        end if
        while count(stdin.lines) > 0
            l = take_first(stdin.lines)
            print string(byte_count(l)) + " " + hex_encode(l)
        end while
        if stdin.closed then
            print "closed; at most " + string(g.most) + " line per delivery"
        end if
    end watch
end program
