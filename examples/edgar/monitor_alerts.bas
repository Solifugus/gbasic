' monitor_alerts — the shared, PURE alerting policy for the EDGAR watcher monitor
' (edgar_design.md §7). Loaded by BOTH the live monitor.bas and its offline test
' harness so the two cannot drift: the watcher wiring in each is identical and
' the only classification is forensics.events (facts-free — the WP-EVT-1 surface).
library monitor_alerts
    load forensics from "../../stdlib/forensics.bas"

    ' an array of filing records {form,filed,accession,period,items} -> the
    ' submissions frame shape forensics.events consumes.
    function frame(rows)
        f = {}
        f["form"] = []
        f["filed"] = []
        f["accession"] = []
        f["period"] = []
        f["items"] = []
        for each r in rows
            append(f["form"], r["form"])
            append(f["filed"], r["filed"])
            append(f["accession"], r["accession"])
            append(f["period"], r["period"])
            append(f["items"], r["items"])
        end for
        return f
    end function

    ' classify the whole rolling history into red-flag events (facts-free, so
    ' clustering — e.g. 5.02 officer exodus — becomes visible as arrivals accrue).
    function classify(rows)
        return forensics.events(frame(rows))
    end function

    ' which alert channel an event kind routes to. The fire alarm (4.02) and a
    ' can't-file-on-time notice are CRITICAL; the rest are WARNING.
    function severity(kind)
        if kind = "non_reliance" then
            return "critical"
        end if
        if kind = "nt_filing" then
            return "critical"
        end if
        return "warning"
    end function

    ' one alert line for event i of an events frame.
    function line(ev, i)
        return ev["kind"][i] + " " + ev["date"][i] + " acc=" + ev["accession"][i] + " :: " + ev["detail"][i]
    end function

    ' stable dedup key for an event (an accession fires each kind at most once).
    function key(ev, i)
        return ev["accession"][i] + "|" + ev["kind"][i]
    end function
end library
