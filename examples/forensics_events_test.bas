' WP-EVT-1 — forensics.events: the submissions-side red-flag surface exposed on
' its own, facts-free. This is the §7 monitor's classifier — it must raise an
' NT / amendment / 4.01 / 4.02 / 5.02-cluster alert on an incoming filing stream
' WITHOUT the filer's companyfacts. events() is exactly the submissions rows of
' flags() (same kinds, columns, ordering, window), with no facts-side detectors.
program main(args)
    load forensics from "../stdlib/forensics.bas"

    ps{file}= "examples/fixtures/edgar/flags_synthetic_subs.json"
    subs = decode(join(read_lines(ps), "\n"))

    ev = forensics.events(subs)
    print("== events(subs) — submissions-side only, no companyfacts ==")
    i = 0
    while i < count(ev["kind"])
        print(ev["kind"][i] + " | " + ev["date"][i] + " | acc=" + ev["accession"][i] + " | " + ev["detail"][i])
        i = i + 1
    end while
    print("rows: " + string(count(ev["kind"])))

    ' window parameter carries through to the 5.02 cluster
    ev10 = forensics.events_window(subs, 10)
    print("== events_window(subs, 10) ==")
    print("rows: " + string(count(ev10["kind"])) + " (cluster dissolved)")

    ' monitor use: classify a SINGLE incoming filing, no facts in hand ---------
    incoming = {}
    incoming["form"] = ["8-K"]
    incoming["filed"] = ["2026-07-04"]
    incoming["accession"] = ["0000000000-26-000042"]
    incoming["period"] = [""]
    incoming["items"] = ["4.02,9.01"]
    hit = forensics.events(incoming)
    print("== monitor: classify one incoming 8-K (the fire alarm) ==")
    print("kind: " + hit["kind"][0] + " | detail: " + hit["detail"][0])
end program
