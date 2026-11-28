' One frame of N rows in N groups, summarized or joined to a frame of N rows:
' linear by hash, quadratic as the scans were. Prints the rows answered.
load frame

function total(sub)
    return sum(sub.v)
end function

program main(args)
    n = number(args[0])
    k = []
    v = []
    for i = 0 to n - 1
        append(k, "c" + string(i))
        append(v, i)
    next
    df = { k: k, v: v }
    if args[1] = "summarize" then
        print len(frame.summarize(df, "k", { total: total }).k)
    else
        print len(frame.join(df, { k: k, w: v }, "k").w)
    end if
end program
