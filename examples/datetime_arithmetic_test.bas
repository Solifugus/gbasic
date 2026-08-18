' The §4 arithmetic floor of docs/datetime_design.md: the accountant's month
' rule, datetime subtraction, duration algebra, and duration comparison.
'
' SELF-CHECKING (the PLAT-EQ lesson): every check states its own expected value
' and prints ok or a MISMATCH naming both sides, so the golden cannot enshrine
' a wrong answer. This matters doubly here because the behaviour this replaces
' was exactly the kind a golden defends: before the fix, EVERY duration
' equality was true and EVERY ordering was false -- both sides of the
' comparison fell through to numeric coercion and became 0.

function check(label, got, want)
    if string(got) = string(want) then
        print "ok " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + " want " + string(want)
    end if
    return 0
end function

program main(args)
    ' --- the accountant's rule: years/months first, CLAMP, then exact parts ---
    jan31 (date)= "2026-01-31"
    feb29 (date)= "2024-02-29"
    mar31 (date)= "2026-03-31"
    feb01 (date)= "2026-02-01"

    x = check("Jan31 + 1 month        ", jan31 + 1 month, "2026-02-28")
    x = check("Jan31 + 1 month + 1 day", jan31 + 1 month + 1 day, "2026-03-01")
    x = check("Feb29 + 1 year         ", feb29 + 1 year, "2025-02-28")
    x = check("Mar31 + 1 month        ", mar31 + 1 month, "2026-04-30")
    x = check("Feb01 + 1 month        ", feb01 + 1 month, "2026-03-01")
    x = check("Mar31 - 1 month        ", mar31 - 1 month, "2026-02-28")
    x = check("Jan31 + 13 months      ", jan31 + 1 year 1 month, "2027-02-28")

    ' Clamping is LOSSY, on purpose: the round trip does not hold at month-end.
    ' Pinned so nobody "fixes" it into something worse later.
    x = check("(Jan31+1mo)-1mo != Jan31", (jan31 + 1 month) - 1 month, "2026-01-28")

    ' --- datetime - datetime -> exact duration ---
    a (date)= "2026-03-15 10:00:00"
    b (date)= "2026-03-14 08:30:00"
    x = check("dt - dt                ", a - b, "1 day 1 hour 30 minutes")
    x = check("dt - dt (negative)     ", b - a, "-1 day -1 hour -30 minutes")
    x = check("dt - itself            ", a - a, "0 seconds")

    ' --- duration algebra ---
    x = check("1h + 30m               ", (1 hour) + (30 minutes), "1 hour 30 minutes")
    x = check("2h - 30m               ", (2 hours) - (30 minutes), "1 hour 30 minutes")
    x = check("45m * 4                ", (45 minutes) * 4, "3 hours")
    x = check("4 * 45m                ", 4 * (45 minutes), "3 hours")
    x = check("1 day / 2              ", (1 day) / 2, "12 hours")
    x = check("30m - 2h (signed)      ", (30 minutes) - (2 hours), "-1 hour -30 minutes")
    x = check("1mo + 1mo              ", (1 month) + (1 month), "2 months")
    x = check("14mo canonical         ", (7 months) + (7 months), "1 year 2 months")
    x = check("months scale integer   ", (1 month) * 3, "3 months")
    x = check("mixed calendar+exact   ", (1 month) + (2 days), "1 month 2 days")

    ' --- duration ordering: a total order on EXACT durations ---
    x = check("90m > 1h               ", (90 minutes) > (1 hour), true)
    x = check("1h < 90m               ", (1 hour) < (90 minutes), true)
    x = check("2h > 90m               ", (2 hours) > (90 minutes), true)
    x = check("25h > 1 day            ", (25 hours) > (1 day), true)
    x = check("1 week >= 7 days       ", (1 week) >= (7 days), true)
    x = check("59m <= 1h              ", (59 minutes) <= (1 hour), true)

    ' --- duration equality: (months, seconds) pairs, canonicalised ---
    x = check("90m = 1h30m            ", (90 minutes) = (1 hour 30 minutes), true)
    x = check("1yr = 12mo             ", (1 year) = (12 months), true)
    x = check("1wk = 7d               ", (1 week) = (7 days), true)
    x = check("3wk = 21d              ", (3 weeks) = (21 days), true)
    x = check("1mo = 30d is FALSE     ", (1 month) = (30 days), false)
    x = check("1mo = 31d is FALSE     ", (1 month) = (31 days), false)
    x = check("1mo != 30d             ", (1 month) != (30 days), true)

    ' contains routes through the same comparison, so it inherits the fix.
    slots = [30 minutes, 1 hour, 90 minutes]
    x = check("contains 60m           ", contains(slots, 60 minutes), true)
    x = check("contains 2h            ", contains(slots, 2 hours), false)
end program
