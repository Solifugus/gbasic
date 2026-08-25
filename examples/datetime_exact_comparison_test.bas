same_a{date}= "2026-05-15"
same_b{date}= "2026-05-15"
same_c{date}= "2026-05-15"

if same_a = same_b and same_b = same_c and same_a = same_c then
    print("transitive equality")
end if

day_value{date}= "2026-05-15"
timestamp_value{date}= "2026-05-15 12:05:03"
month_value{date}= "2026-05"
year_value{date}= "2026"
june_value{date}= "2026-06-20"

if day_value != timestamp_value then
    print("same day different precision not equal")
end if

if year_value != day_value then
    print("same year different precision not equal")
end if

if year_value != june_value and day_value != june_value then
    print("old non-transitive chain removed")
end if

if day_value {day}= timestamp_value then
    print("brace day lens")
end if

if day_value{day}= timestamp_value then
    print("old day lens")
end if

if month_value {month}= day_value then
    print("brace month lens")
end if

if month_value{month}= day_value then
    print("old month lens")
end if

if day_value < timestamp_value then
    print("instant ordering")
end if

y{date}= "2026"
m{date}= "2026-01"
d{date}= "2026-01-01"
s{date}= "2026-01-01 00:00:00"
sorted = sort([s, d, y, m])
print(sorted[0])
print(sorted[1])
print(sorted[2])
print(sorted[3])

d1{date}= "2026-05-15"
d2{date}= "2026-05-15 00:00:00"
d3{date}= "2026-05-15"
unique_values = unique([d1, d2, d3])
print(len(unique_values))
print(unique_values[0])
print(unique_values[1])
