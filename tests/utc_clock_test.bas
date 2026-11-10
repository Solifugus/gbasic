' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' Every instant gBASIC makes is UTC (2026-11-10): now(), from_epoch(), file
' times, and epoch(dt)/number(dt) read a datetime as UTC. now(zone),
' to_zone(dt, zone) and zone_offset(dt) are the edges, and "local" names the
' system's own zone. tests/run_utc_clock.sh runs this under several TZ
' settings; every value under "-- stable" must print the same bytes in all of
' them, and args[1] is the system zone's offset in minutes at 2027-04-06
' 12:52:00 UTC, computed by Python's zoneinfo.
'
' args[0] is a scratch directory.

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    dir = args[0]
    want_offset = number(args[1])

    print "-- the clock"
    gap = epoch(now()) - epoch()
    check("epoch(now()) is the current epoch (within a second)", gap >= -1 and gap <= 0, true)
    gap = epoch(now()) - epoch(now("UTC"))
    check("now() is now(\"UTC\")", gap >= -1 and gap <= 1, true)
    gap = epoch(now("local"), "local") - epoch()
    check("now(\"local\") read in \"local\" is the same instant", gap >= -1 and gap <= 0, true)
    t = now()
    gap = (now("local") - t - zone_offset(t)).total_seconds
    check("now(\"local\") is ahead of now() by the system offset now", gap >= 0 and gap <= 1, true)

    print ""
    print "-- the edges, against Python's zoneinfo"
    d = {datetime}"2027-04-06T12:52:00Z"
    check("zone_offset(dt) is the system zone's offset at that instant",
          zone_offset(d) = (1 minute) * want_offset, true)
    check("to_zone(dt, \"local\") is dt plus that offset", to_zone(d, "local") = d + (1 minute) * want_offset, true)
    check("from_zone(..., \"local\") undoes it", from_zone(to_zone(d, "local"), "local") = d, true)
    check("epoch(civil, \"local\") reads local civil time", epoch(to_zone(d, "local"), "local"), epoch(d))

    print ""
    print "-- files, held to stat by the runner"
    f {file}= join_path(dir, "stamped.txt")
    write(f, "x")
    stamp = {datetime}"2026-03-08T07:30:00Z"
    set_mtime(f, stamp)
    check("file_mtime gives back what set_mtime stamped", file_mtime(f) = stamp, true)
    for each e in files(dir, "stamped.txt")
        check("a directory entry's modified agrees", e.modified = stamp, true)
    next
    print "stamped " + string(epoch(stamp))

    print ""
    print "-- stable"
    print from_epoch(0)
    print from_epoch(1700000000)
    print from_epoch(1772951400)                   ' 2026-03-08 06:30 UTC, New York's spring-forward hour
    print epoch({datetime}"2000-01-01 00:00:00")
    print number({datetime}"2026-03-08 07:30:00")
    print epoch({datetime}"2026-11-01 06:30:00")   ' New York's repeated hour, in UTC
    print epoch(from_epoch(1793511000))
    print {datetime}"2027-04-06T07:52:00-05:00"
    print to_zone(d, "Asia/Tokyo")
    print zone_offset(d, "America/New_York")
    print epoch(stamp)
    print file_mtime(f)
    load mail
    print mail.format_date(d, "UTC")
    print mail.format_date(d, "Asia/Kolkata")

    print ""
    print "-- refused"
    on error goto next
    x = now("Local")
    check("\"Local\" is not \"local\"", contains(error.message, "unknown timezone 'Local'"), true)
    error.clear()
    on error stop

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
