words = []
if len(words) = 0 then
    print("empty")
end if

words = ["lamp"]
if find(words, "lamp") = nothing then
    print("missing")
end if
if find(words, "key") = nothing then
    print("key missing")
end if

scores = [80, 90, 70]
if mean(scores) >= 80 then
    print("good")
end if

name = "Joe"
if name(caseless) = "joe" then
    print("modifier ok")
end if

modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

amount = 1.234
expected = 1.23
if amount(rounded 2) = expected then
    print("parameterized modifier ok")
end if
