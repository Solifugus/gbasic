' Recipe 4 — How far apart? Two different questions, two different answers.
'
' datetime - datetime gives the EXACT elapsed time, in days and smaller --
' never months, because a month has no fixed length. "How many months apart"
' is a CALENDAR question, answered by dates.between, which is consistent with
' the clamping rule by construction.

program main(args)
    load dates from "../../stdlib/dates.bas"

    a {date}= "2026-03-14 08:30:00"
    b {date}= "2026-03-15 10:00:00"
    print "exact gap    : " + (b - a)
    print "backwards    : " + (a - b)

    ' Days until a deadline -- exact division of an exact duration.
    today {date}= "2026-08-17"
    deadline {date}= "2026-09-30"
    gap = deadline - today
    print "days left    : " + (gap.total_seconds / 86400)
    print "same, direct : " + dates.between(today, deadline, "days")

    ' Ages and anniversaries are calendar arithmetic.
    born {date}= "1990-06-15"
    print "age in years : " + dates.between(born, today, "years")
    print "age in months: " + dates.between(born, today, "months")

    ' The month count agrees with the operator, clamping included: Jan 31 ->
    ' Feb 28 counts as one month exactly BECAUSE Jan 31 + 1 month is Feb 28.
    jan31 {date}= "2026-01-31"
    feb28 {date}= "2026-02-28"
    print "Jan31->Feb28 : " + dates.between(jan31, feb28, "months") + " month(s)"
end program
