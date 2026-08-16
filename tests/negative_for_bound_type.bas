' Bounds must be numbers. Coercing a string here would turn a typo into a loop
' that runs a surprising number of times rather than an error.
for i = 1 to "five"
    print i
end for
