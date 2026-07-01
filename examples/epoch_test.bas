' epoch / datetime-to-number conversion. Unix epoch is timezone-independent
' (an absolute instant), and mktime(localtime(t)) == t on any machine, so these
' checks are deterministic regardless of the host timezone. Absolute wall-clock
' rendering (from_epoch's fields) IS timezone-dependent and is deliberately not
' printed here.
program demo(args)
    ' epoch() is an integer number of seconds
    e = epoch()
    print("epoch_int " + string(e = floor(e)))

    ' number(now()) agrees with epoch() (allow a 1-2s boundary)
    d = number(now()) - e
    print("now_matches " + string(d >= 0 - 2 and d <= 2))

    ' datetime <-> epoch round-trips exactly (timezone cancels out)
    print("rt_fixed " + string(number(from_epoch(1751313600)) = 1751313600))
    print("rt_now " + string(number(from_epoch(e)) = e))

    ' epoch arithmetic: durations are exact
    start = 1000000000
    later = number(from_epoch(start)) + 3600
    print("plus_hour " + string(later - start))
    print("day_seconds " + string(number(from_epoch(1000086400)) - number(from_epoch(1000000000))))
end program
