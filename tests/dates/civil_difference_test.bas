' THE DIFFERENCE OF TWO CIVIL DATETIMES DOES NOT DEPEND ON A TIMEZONE.
'
' A gBASIC `datetime` is CIVIL and carries no zone -- which is why `epoch(dt,
' zone)` exists and takes one. So `b - a` is calendar arithmetic and the machine's
' zone has no business in it. It was in it: the subtraction went through `mktime`
' with `tm_isdst = -1`, so an interval spanning a spring-forward transition came
' out AN HOUR SHORT in whichever zone the machine happened to be set to.
'
' THE CONSEQUENCE WAS A WRONG ANSWER IN A FINANCE LIBRARY, not a cosmetic one:
'
'   dates.between({date}"2026-02-01", {date}"2026-04-02", "days")
'     America/New_York: 59.958333        UTC: 60
'
' `stdlib/credit.bas`'s delinquency ladder tests `days >= 60`, so in a US timezone
' a loan SIXTY DAYS PAST DUE was bucketed `dpd_30` -- one step too healthy,
' silently, and differently in different offices. `examples/credit_cookbook/
' 02_delinquency.out` had the New York answer committed as expected.
'
' FOUND BY CI, which had been red on every push for twenty-odd runs while the
' local gate was green, because the local gate runs in one timezone and a golden
' captured there agrees with itself. THIS FIXTURE IS RUN IN SEVERAL ZONES by
' tests/run_dates.sh -- the assertion cannot live inside one process, since the
' zone is chosen before gBASIC starts.

' A RECORD, because a function cannot rebind an outer scalar -- gBASIC has no
' closures, so `checks = checks + 1` in here would write a silent function-local
' and the tally would stay at zero. That is run_core.sh's read-then-shadow rule,
' and the first draft of this file got it wrong.
tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        tally.bad = tally.bad + 1
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
    end if
    return nothing
end function

' A local copy of credit's ladder, so this fixture does not depend on loading a
' finance library to assert a date property -- the cascade mistake made once
' already in run_sort_records. DECLARED BEFORE USE: a script with no `program`
' block registers functions as it reaches them.
function _bucket(days)
    if days >= 120 then
        return "dpd_120_plus"
    else if days >= 90 then
        return "dpd_90"
    else if days >= 60 then
        return "dpd_60"
    else if days >= 30 then
        return "dpd_30"
    end if
    return "current"
end function

load dates

' --- the interval that exposed it -------------------------------------------
' 2026-03-08 is the US spring-forward. Both endpoints are plain dates, so there
' is no instant here to be ambiguous about -- only a count of days.
feb {date}= "2026-02-01"
apr {date}= "2026-04-02"
ok("across spring-forward   ", dates.between(feb, apr, "days"), 60)
ok("and the raw duration    ", (apr - feb).total_seconds, 60 * 86400)

' --- the autumn transition, which errs the other way ------------------------
' 2026-11-01 is fall-back, where a local-time subtraction gains an hour instead
' of losing one. Asserted separately: a fix that clamped the sign would pass the
' spring case alone.
oct {date}= "2026-10-15"
nov {date}= "2026-11-15"
ok("across fall-back        ", dates.between(oct, nov, "days"), 31)

' --- a whole year, which crosses both --------------------------------------
y0 {date}= "2026-01-01"
y1 {date}= "2027-01-01"
ok("a full year             ", dates.between(y0, y1, "days"), 365)

' --- the ladder itself, which is what the defect reached --------------------
' The boundary is the thing: 59 days is dpd_30 and 60 is dpd_60, so an interval
' that should be exactly 60 must not land a fraction below it.
ok("59 days is dpd_30       ", _bucket(59), "dpd_30")
ok("60 days is dpd_60       ", _bucket(60), "dpd_60")
ok("and the real interval   ", _bucket(dates.between(feb, apr, "days")), "dpd_60")

' --- BEFORE 1970, which the same fix repaired ------------------------------
' `mktime` refuses every instant before 1970 on Windows, so this subtraction
' raised there and answered here; `timegm` takes any year on both.
old0 {date}= "1950-03-01"
old1 {date}= "1950-05-01"
ok("a pre-1970 interval     ", dates.between(old0, old1, "days"), 61)

' --- CONTROLS: what must NOT have changed ---------------------------------
' A time-only value still has no epoch to subtract from.
on error goto next
t0 {time}= "10:00:00"
t1 {date}= "2026-01-01"
x = t1 - t0
ok("time-only still refused ", contains(error.message, "no epoch"), true)
error.clear()
' And an ordinary same-month interval is unchanged, so the fix is not a shift.
m0 {date}= "2026-06-01"
m1 {date}= "2026-06-11"
ok("an ordinary interval    ", dates.between(m0, m1, "days"), 10)
' Months are a calendar question and were never seconds-based.
ok("months unaffected       ", dates.between(feb, apr, "months"), 2)

print("")
print("checks: " + string(tally.checks))
print("mismatches: " + string(tally.bad))
