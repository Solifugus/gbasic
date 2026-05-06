modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

modifier split(sep) for assign
    return value
end modifier

if find("banana", "zz") = nothing then
    print("missing")
end if

if find("banana", "na") != nothing then
    print("found")
end if

name = "Joe"
if name(caseless) = "joe" then
    print("caseless")
end if

amount = 1.234
expected = 1.23
if amount(rounded 2) = expected then
    print("rounded")
end if

x = find("banana", "na")
print(x)

x(split ",") = "a,b,c"
print(x)
