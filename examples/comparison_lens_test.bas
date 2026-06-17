modifier rounded(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

modifier rounded to(n) for compare
    return compare(round(left, n), operator, round(right, n))
end modifier

name = "Bob"
if name {caseless}= "bob" then
    print("caseless equals")
end if
if name {caseless}!= "alice" then
    print("caseless not equals")
end if

amount = 1.234
expected = 1.23
larger = 1.24

if amount {rounded 2}= expected then
    print("rounded equals")
end if
if amount {rounded 2}<= expected then
    print("rounded less or equal")
end if
if amount {rounded 2}>= expected then
    print("rounded greater or equal")
end if
if amount {rounded 2}< larger then
    print("rounded less")
end if
if larger {rounded to 2}> amount then
    print("rounded to greater")
end if
if amount {rounded to 2}!> expected then
    print("rounded not greater")
end if
if larger {rounded to 2}!< amount then
    print("rounded not less")
end if

d(date)= "2026-05-15 14:30:20"
later(date)= "2026-05-16"
next_month(date)= "2026-06"
next_year(date)= "2027"

if d {day}= "2026-05-15" then
    print("same day")
end if
if d {day}< later then
    print("day less")
end if
if d {month}= "2026-05" then
    print("same month")
end if
if d {month}< next_month then
    print("month less")
end if
if d {year}= "2026" then
    print("same year")
end if
if d {year}< next_year then
    print("year less")
end if
