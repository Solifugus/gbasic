' 2000 calls of dates.days_between over a span of DAYS_SPAN days: the
' runner times two spans, which must cost the same.
load dates
a{day}= "1990-01-01"
b = a + number(env("DAYS_SPAN")) * 1 day
total = 0
for i = 1 to 2000
    total = total + dates.days_between(a, b)
end for
print total
