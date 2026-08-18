program demo(args)
    load dates from "../stdlib/dates.bas"

    today(date)= "2026-05-15"

    end = 5
    next = 10
    print(end + next)

    x(end of month)= today
    y(next monday)= today
    print(x)
    print(y)

end:
    print("ok")

    end = 1
    if end = 1 then
        print("works")
    end if

    ' `loop` and `until` became keywords when `do ... loop` landed, but neither
    ' can START a statement, so both stay usable as ordinary names -- as
    ' variables, and as LABELS. The label case is not hypothetical: four
    ' `loop:`/`goto loop` pairs in stdlib/dates.bas stopped parsing when the
    ' keywords were added, because `label_statement` accepted the wider name set
    ' and `goto`/`gosub` still demanded a bare IDENT.
    loop = 3
    until = 4
    print(loop + until)
    print(counts_to_two())
end program

function counts_to_two()
    i = 0
loop:
    i = i + 1
    if i < 2 then goto loop
    return i
end function
