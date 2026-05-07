if true then
    print("true branch")
else
    print("bad")
end if

if false then
    print("bad")
else
    print("false branch")
end if

x = 2
if x > 1 then
    if x = 2 then
        print("nested true")
    else
        print("bad")
    end if
else
    print("bad")
end if

if x < 1 then
    print("bad")
else
    if x = 2 then
        print("nested else")
    else
        print("bad")
    end if
end if
