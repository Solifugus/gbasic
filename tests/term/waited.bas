' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' The terminal's events, waited for (tests/run_term.sh, tiers keys, mouse,
' paste, resize): each event term.wait answers is written as one line to
' standard error, which the driver points at a file, so it never touches the
' screen. "q" ends it. The driver tests/term/drive.py is the oracle.
load term

function show(e)
    if e.kind = "key" then
        m = ""
        if e.ctrl then
            m = m + "ctrl+"
        end if
        if e.alt then
            m = m + "alt+"
        end if
        if e.shift then
            m = m + "shift+"
        end if
        return "key " + m + e.key + " [" + e.text + "]"
    end if
    if e.kind = "mouse" then
        return "mouse " + e.action + " " + string(e.button) + " " + string(e.column) + "," + string(e.row)
    end if
    if e.kind = "paste" then
        return "paste " + hex_encode(e.text)
    end if
    return "resize " + string(e.columns) + "x" + string(e.rows)
end function

term.open({ mouse: true })
s = term.size()
print to error "ready " + string(s.columns) + "x" + string(s.rows)
going = true
while going
    e = term.wait(10 seconds)
    if is_unknown(e) then
        print to error "timeout"
        going = false
    else
        print to error show(e)
        if e.kind = "resize" then
            s = term.size()
            print to error "size " + string(s.columns) + "x" + string(s.rows)
        end if
        if e.kind = "key" and e.key = "q" then
            going = false
        end if
    end if
end while
term.close()
print to error "closed"
