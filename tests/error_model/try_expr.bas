' `try expr` (2026-11-07, Transward §8.2): the value, or nothing when it
' raised; what it caught is readable by the next check and never raised again
' at the return; it leaves the frame's `on error` policy as it was.
function boom(tag)
    error "boom " + tag
    return 1
end function

' THE TRAP IT CLOSES: best-effort cleanup that does not look. Under
' `on error goto next` the unclaimed raise came back at the return; under
' `try` it does not, with or without a mode.
function cleanup_plain()
    try boom("in cleanup")
    return "plain cleanup returned"
end function
function cleanup_under_next()
    on error goto next
    x = try boom("under next")
    return "cleanup under goto next returned"
end function

' THE POLICY AROUND IT IS UNDISTURBED: a label still catches the next raise
function label_kept()
    on error goto handler
    x = try boom("tried")
    y = boom("labelled")
    return "label not taken"
handler:
    return "label caught: " + error.message
end function

x = try boom("a")
print "1 " + string(x)
if error then
    print "2 read: " + error.message
end if
if error then
    print "WRONG: read twice"
end if
print "3 " + string(try 1 + 2)
z = try boom("b") + 1
print "4 " + string(z)
e = error
print "5 snapshot: " + e.message
a = try boom("c")
b = try 5
print "6 a success clears it: " + string(error)
print "7 " + cleanup_plain()
print "8 " + cleanup_under_next()
print "9 " + label_kept()
r = { try: "a field" }
print "10 " + r.try
q = try boom("d")
error.clear()
print "11 cleared: " + string(error)
