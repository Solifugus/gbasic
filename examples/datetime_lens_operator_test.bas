base(date)= "2026-05-15 14:30:20"
same_day_later(date)= "2026-05-15 23:59:59"
next_day(date)= "2026-05-16 00:00:00"
previous_day(date)= "2026-05-14 23:59:59"
same_month_later(date)= "2026-05-31 23:59:59"
next_month(date)= "2026-06-01 00:00:00"
same_year_later(date)= "2026-12-31 23:59:59"
next_year(date)= "2027-01-01 00:00:00"
same_hour_later(date)= "2026-05-15 14:59:59"
next_hour(date)= "2026-05-15 15:00:00"
same_minute_later(date)= "2026-05-15 14:30:59"
next_minute(date)= "2026-05-15 14:31:00"
same_second(date)= "2026-05-15 14:30:20"
next_second(date)= "2026-05-15 14:30:21"

if base {day}= same_day_later then
    print("day equals")
end if
if base {day}< next_day then
    print("day less")
end if
if base {day}<= same_day_later then
    print("day less or equal")
end if
if base {day}> previous_day then
    print("day greater")
end if
if base {day}>= same_day_later then
    print("day greater or equal")
end if
if base {day}!= next_day then
    print("day not equal")
end if
if base {day}!< same_day_later then
    print("day not less")
end if
if base {day}!> same_day_later then
    print("day not greater")
end if

if base {month}= same_month_later then
    print("month equals")
end if
if base {month}< next_month then
    print("month less")
end if

if base {year}= same_year_later then
    print("year equals")
end if
if base {year}< next_year then
    print("year less")
end if

if base {hour}= same_hour_later then
    print("hour equals")
end if
if base {hour}< next_hour then
    print("hour less")
end if

if base {minute}= same_minute_later then
    print("minute equals")
end if
if base {minute}< next_minute then
    print("minute less")
end if

if base {second}= same_second then
    print("second equals")
end if
if base {second}< next_second then
    print("second less")
end if

if base != same_day_later then
    print("bare exact unchanged")
end if

if base(day)= same_day_later then
    print("old day lens still works")
end if
