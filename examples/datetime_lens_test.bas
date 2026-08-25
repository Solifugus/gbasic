d{date}= "2026-05-15 14:30:20"

y{year}= d
print y

if d{month}= "2026-05" then
    print "same month"
end if

if d{day}= "2026-05-15" then
    print "same day"
end if

if d{hour}= "2026-05-15 14" then
    print "same hour"
end if

if d{minute}= "2026-05-15 14:30" then
    print "same minute"
end if

if d{second}= "2026-05-15 14:30:20" then
    print "same second"
end if
