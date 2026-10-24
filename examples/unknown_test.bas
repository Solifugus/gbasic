x = unknown

if is_unknown(x) then
    print("unknown")
end if

if not is_unknown(x) then
    print("not unknown")
end if

print(x)

print(unknown != nothing)        ' unknown: a missing fact compared is a missing answer
if is_unknown(unknown) and is_nothing(nothing) then
    print("distinct")
end if

x = "hello"
x = unknown

if is_unknown(x) then
    print("stored unknown")
end if
