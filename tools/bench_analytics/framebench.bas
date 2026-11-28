' The analytics tasks through stdlib/frame.bas, as its verbs are meant to be
' used: read_csv, with_column, filter, summarize, join, sort_by.
' usage: gbasic framebench.bas ORDERS.csv CUSTOMERS.csv TASK
load frame

function amount_of(row)
    return row.qty * row.price
end function

function west_big(row)
    return row.region = "West" and row.qty >= 10
end function

function month_of(row)
    return left(string(row.day), 7)
end function

function n_rows(sub)
    return len(sub.id)
end function

function total(sub)
    return sum(sub.amount)
end function

program main(args)
    df = frame.read_csv(args[0])
    task = args[2]
    if task = "load" then
        print string(len(df.id)) + " rows"
        exit(0)
    end if
    df = frame.with_column(df, "amount", amount_of)
    if task = "filter" then
        m = frame.filter(df, west_big)
        print string(len(m.id)) + " " + string(sum(m.amount))
    else if task = "group" then
        g = frame.sort_by(frame.summarize(df, "region", { n: n_rows, total: total }), "region")
        out = []
        for each r, i in g.region
            append(out, r + ":" + string(g.n[i]) + ":" + string(g.total[i]))
        next
        print join(out, " ")
    else if task = "bycust" then
        g = frame.summarize(df, "customer", { total: total })
        rows = sort(frame.to_rows(g), { by: ["total", "customer"], descending: ["total"] })
        out = []
        for i = 0 to 4
            append(out, string(rows[i].customer) + ":" + string(rows[i].total))
        next
        print join(out, " ")
    else if task = "top" then
        rows = sort(frame.to_rows(df), { by: ["amount", "id"], descending: ["amount"] })
        out = []
        for i = 0 to 9
            append(out, string(rows[i].id))
        next
        print join(out, " ")
    else if task = "join" then
        c = frame.rename(frame.read_csv(args[1]), { id: "customer" })
        j = frame.join(df, c, "customer")
        g = frame.sort_by(frame.summarize(j, "segment", { total: total }), "segment")
        out = []
        for each s, i in g.segment
            append(out, s + ":" + string(g.total[i]))
        next
        print join(out, " ")
    else if task = "month" then
        df = frame.with_column(df, "month", month_of)
        g = frame.sort_by(frame.summarize(df, "month", { n: n_rows }), "month")
        print string(len(g.month)) + " " + string(g.n[0]) + " " + string(g.n[len(g.n) - 1])
    end if
end program
