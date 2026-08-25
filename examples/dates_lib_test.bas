program demo(args)
    load dates from "../stdlib/dates.bas"

    today{date}= "2026-05-15"

    end{end of month}= today
    print(end)

    next{next monday}= today
    print(next)

    print(dayname(today))
end program
