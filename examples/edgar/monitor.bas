' WP-EVT-2 — the flagship EDGAR watcher monitor (edgar_design.md §7).
'
' A live polling loop feeds a watched inbox; watcher bodies fire synchronously on
' arrival and raise item-code alerts — no other BASIC gets to `watch()` a real
' external event stream and mean it. The watcher wiring below is IDENTICAL to the
' offline harness (examples/edgar/monitor_harness_test.bas); only the source of
' arrivals differs (edgar.poll here, synthetic appends there). Both share the pure
' alerting policy in monitor_alerts.bas and classify with forensics.events (no
' companyfacts on the hot path — the WP-EVT-1 surface).
'
' DEMO PROGRAM: hits the SEC network and sleeps between polls; run by hand, not in
' the test suite. Set your contact string below (SEC requires it) before running:
'   GBASIC_PATH=stdlib ./gbasic examples/edgar/monitor.bas
program main(args)
    load edgar
    load monitor_alerts from "monitor_alerts.bas"

    ' --- config --------------------------------------------------------------
    cik = "0000320193"                       ' Apple Inc.; any 10-digit CIK string
    contact = "Your Name <you@example.com>"  ' SEC requires a real contact
    poll_seconds = 900                       ' 15 minutes; SEC is not a firehose

    e = edgar.session()
    e = edgar.identify(e, contact)

    ' --- watched board + watchers (identical to the harness) -----------------
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

    ' --- polling loop --------------------------------------------------------
    last_seen = unknown
    print("watching CIK " + cik + " every " + string(poll_seconds) + "s — Ctrl-C to stop")
    while true
        subs = edgar.submissions(e, cik)
        fresh = edgar.poll(subs, last_seen)
        ' poll returns newest-first; append oldest-first so the inbox — and thus
        ' the cluster arithmetic — sees arrivals in chronological order.
        j = count(fresh["accession"]) - 1
        while j >= 0
            append(board.inbox, { form: fresh["form"][j], filed: fresh["filed"][j], accession: fresh["accession"][j], period: fresh["period"][j], items: fresh["items"][j] })
            j = j - 1
        end while
        if count(subs["accession"]) > 0 then
            last_seen = subs["accession"][0]
        end if
        sleep(poll_seconds)
    end while
end program
