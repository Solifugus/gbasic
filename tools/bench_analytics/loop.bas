' The analytics tasks as plain gBASIC: the columns read into arrays, then
' loops, with a record as the hash map for groups. Written the way the
' reference's idioms say (csv.records, for each, a record keyed by text).
' usage: gbasic loop.bas ORDERS.csv CUSTOMERS.csv TASK
'   TASK: load | filter | group | bycust | top | join | month

function load_orders(path)
    cols = { id: [], customer: [], region: [], day: [], qty: [], price: [] }
    for each r in csv.records({file}path)
        append(cols.id, number(r.id))
        append(cols.customer, number(r.customer))
        append(cols.region, r.region)
        append(cols.day, r.day)
        append(cols.qty, number(r.qty))
        append(cols.price, number(r.price))
    next
    return cols
end function

' customer id -> segment, as a record keyed by the id's text
function load_segments(path)
    seg = {}
    for each r in csv.records({file}path)
        seg[r.id] = r.segment
    next
    return seg
end function

function task_filter(o)
    n = 0
    total = 0
    for each region, i in o.region
        if region = "West" and o.qty[i] >= 10 then
            n += 1
            total += o.qty[i] * o.price[i]
        end if
    next
    return string(n) + " " + string(total)
end function

function task_group(o)
    count = {}
    total = {}
    for each region, i in o.region
        count[region] = default(count[region], 0) + 1
        total[region] = default(total[region], 0) + o.qty[i] * o.price[i]
    next
    out = []
    for each k in sort(keys(count))
        append(out, k + ":" + string(count[k]) + ":" + string(total[k]))
    next
    return join(out, " ")
end function

function task_bycust(o)
    total = {}
    for each c, i in o.customer
        k = string(c)
        total[k] = default(total[k], 0) + o.qty[i] * o.price[i]
    next
    rows = []
    for each k in keys(total)
        append(rows, { customer: number(k), amount: total[k] })
    next
    rows = sort(rows, { by: ["amount", "customer"], descending: ["amount"] })
    out = []
    for i = 0 to 4
        append(out, string(rows[i].customer) + ":" + string(rows[i].amount))
    next
    return join(out, " ")
end function

function task_top(o)
    rows = []
    for each q, i in o.qty
        append(rows, { id: o.id[i], amount: q * o.price[i] })
    next
    rows = sort(rows, { by: ["amount", "id"], descending: ["amount"] })
    out = []
    for i = 0 to 9
        append(out, string(rows[i].id))
    next
    return join(out, " ")
end function

function task_join(o, seg)
    total = {}
    for each c, i in o.customer
        s = seg[string(c)]
        total[s] = default(total[s], 0) + o.qty[i] * o.price[i]
    next
    out = []
    for each k in sort(keys(total))
        append(out, k + ":" + string(total[k]))
    next
    return join(out, " ")
end function

function task_month(o)
    count = {}
    for each d in o.day
        m = left(d, 7)
        count[m] = default(count[m], 0) + 1
    next
    ks = sort(keys(count))
    return string(len(ks)) + " " + string(count[ks[0]]) + " " + string(count[ks[len(ks) - 1]])
end function

program main(args)
    o = load_orders(args[0])
    task = args[2]
    if task = "load" then
        print string(len(o.id)) + " rows"
    else if task = "filter" then
        print task_filter(o)
    else if task = "group" then
        print task_group(o)
    else if task = "bycust" then
        print task_bycust(o)
    else if task = "top" then
        print task_top(o)
    else if task = "join" then
        print task_join(o, load_segments(args[1]))
    else if task = "month" then
        print task_month(o)
    end if
end program
