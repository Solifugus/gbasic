' dates.days_between against pairs written by python3's datetime (the
' runner makes them per run): each line "a b days", days = (b - a).days.
' Prints each mismatch, then "checks: N" and "mismatches: M".
load dates
lines = split(trim(read(env("DAYS_PAIRS"))), chr(10))
bad = 0
n = 0
for each l in lines
    p = split(l, " ")
    got = dates.days_between(p[0], p[1])
    n = n + 1
    if got != number(p[2]) then
        bad = bad + 1
        print "MISMATCH " + l + " got " + string(got)
    end if
end for
print "checks: " + string(n)
print "mismatches: " + string(bad)
