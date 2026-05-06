x = nothing

if x = nothing then
    print("nothing")
end if

if x != nothing then
    print("not nothing")
end if

if x = nothing then
    print("still nothing")
end if

x = "hello"
x = nothing

if x = nothing then
    print("cleared")
end if

print x
