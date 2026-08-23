if true then print("inline then")

if false then
    print("bad")
else print("block then, inline else")

if false then print("bad")
else print("inline then and else")

if false then print("bad")
else
    print("inline then, block else")
end if

x = 0
if true then x = 7
print(x)

if true then
    if false then print("bad")
else print("else matches nearest if")
end if

function announce()
    print("inline call")
    return
end function

if true then announce()

function choose(flag)
    if flag then return "yes"
    else return "no"
end function

print(choose(true))
print(choose(false))

function jump(flag)
    if flag then goto selected
    return "bad"

selected:
    return "inline goto"
end function

print(jump(true))

function run_subroutine()
    if true then gosub selected
    return "subroutine done"

selected:
    print("inline gosub")
    return
end function

print(run_subroutine())

i = 0
while i < 4
    i = i + 1
    if i = 2 then continue
    if i = 4 then break
    print("loop " + i)
end while

' These branches are not executed, but verify the remaining simple statements
' are accepted by the inline grammar.
if false then load inline_if_unused_library
if false then error "not reached"
if false then on error goto next

print("after inline if")
