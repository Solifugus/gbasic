' The top level, with no `program` block: under GBASIC_COMPILE the whole
' list is compiled (gb_jit_list), so every path out of a top-level
' statement is held here, compiled against interpreted, as flow.bas holds a
' function body's. (A goto, a gosub and a label are refused at the top
' level; flow.bas holds those.)

function twice(x)
    return x * 2
end function

on error goto next
q = 1 / 0
if error then
    print "absorbed: " + error.message
end if

s = 0
for i = 1 to 5
    if i = 4 then
        continue
    end if
    s = s + twice(i)
end for
print "for: " + string(s)

w = 0
while true
    w = w + 1
    if w = 3 then
        break
    end if
end while
print "while: " + string(w)

' a function declared below its first use at the top level is registered
' when the walk reaches it, so this call is an error the walk absorbs
r = later(1)
if error then
    print "not yet: " + error.message
end if

function later(x)
    return x + 1
end function

print "now: " + string(later(1))

' raises left unchecked under `goto next`: the walk goes on, and the top
' frame reports the last one when the program ends (PLAT-ERR rule 2)
q = 1 / 0
print "after the raise"
q = 1 / 0
