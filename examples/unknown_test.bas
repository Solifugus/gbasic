x = unknown

if is_unknown(x) then
    print("unknown")
end if

if not is_unknown(x) then
    print("not unknown")
end if

print(x)

if unknown != nothing then
    print("distinct")
end if

x = "hello"
x = unknown

if is_unknown(x) then
    print("stored unknown")
end if
