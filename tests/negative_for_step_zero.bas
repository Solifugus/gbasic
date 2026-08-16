' `step 0` can never reach the limit, so it is a hang. Refused instead: a hang
' is the least debuggable outcome a loop can have.
for i = 1 to 5 step 0
    print i
end for
