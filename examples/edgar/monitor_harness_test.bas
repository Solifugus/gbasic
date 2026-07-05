' WP-EVT-2 — offline harness for the EDGAR watcher monitor (edgar_design.md §7).
' Drives the SAME watcher logic as the live monitor.bas by appending synthetic
' filings to the watched inbox — NO network, NO sleep. Asserts the alert output.
'
' Design: a watched inbox (the poll queue) and two watched alert channels. On
' each arrival the inbox watcher re-classifies the whole rolling history via
' monitor_alerts.classify (forensics.events, facts-free) and routes any NEW event
' to its channel; the channel watchers print. Channel watchers are cursor-based
' so output is exact regardless of how watcher drains coalesce.
program main(args)
    load monitor_alerts from "monitor_alerts.bas"

    board = { inbox: [], critical: [], warning: [] }
    alerted = {}
    crit_seen = 0
    warn_seen = 0

    watch(board.critical)
        while crit_seen < count(board.critical)
            print("[CRITICAL] " + board.critical[crit_seen])
            crit_seen = crit_seen + 1
        end while
    end watch

    watch(board.warning)
        while warn_seen < count(board.warning)
            print("[WARNING]  " + board.warning[warn_seen])
            warn_seen = warn_seen + 1
        end while
    end watch

    watch(board.inbox)
        if count(board.inbox) > 0 then
            ev = monitor_alerts.classify(board.inbox)
            i = 0
            while i < count(ev["kind"])
                k = monitor_alerts.key(ev, i)
                if not has(alerted, k) then
                    alerted[k] = true
                    sev = monitor_alerts.severity(ev["kind"][i])
                    ln = monitor_alerts.line(ev, i)
                    if sev = "critical" then
                        append(board.critical, ln)
                    else
                        append(board.warning, ln)
                    end if
                end if
                i = i + 1
            end while
        end if
    end watch

    ' --- synthetic filing stream (arrivals, newest last) ---------------------
    print("== EDGAR monitor: synthetic filing stream ==")
    append(board.inbox, { form: "10-K",    filed: "2024-02-28", accession: "0000000000-24-000003", period: "2023-12-31", items: "" })
    append(board.inbox, { form: "8-K",     filed: "2024-04-20", accession: "0000000000-24-000007", period: "",           items: "4.02,9.01" })
    append(board.inbox, { form: "NT 10-K", filed: "2024-05-10", accession: "0000000000-24-000001", period: "2024-03-31", items: "" })
    append(board.inbox, { form: "8-K",     filed: "2024-05-01", accession: "0000000000-24-000011", period: "",           items: "5.02" })
    append(board.inbox, { form: "8-K",     filed: "2024-05-20", accession: "0000000000-24-000012", period: "",           items: "5.02,9.01" })

    print("== totals ==")
    print("critical: " + string(count(board.critical)) + " warning: " + string(count(board.warning)))
end program
