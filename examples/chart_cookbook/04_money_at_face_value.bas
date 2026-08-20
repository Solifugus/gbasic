' Recipe 4 — Money plots at face value; the axis gets thousands separators.
'
' Frames that come out of the spreadsheet pipeline carry real money values.
' A chart accepts them directly -- no conversion dance -- and the axis
' formatter writes 1,250 rather than 1250. (Text columns are still refused:
' a plausible wrong picture is worse than an error.)

program main(args)
    load chart from "../../stdlib/chart.bas"

    a(USD)= 1200.50
    b(USD)= 1350.25
    c(USD)= 1180.75
    balances = { month: [1, 2, 3], balance: [a, b, c] }
    svg = chart.line(balances, "month", "balance")

    print "separated axis label: " + string(contains(svg, ">1,250</text>"))
    print string(len(svg)) + " chars"
end program
