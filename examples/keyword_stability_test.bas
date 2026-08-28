program demo(args)
    load dates from "../stdlib/dates.bas"

    today{date}= "2026-05-15"

    end = 5
    next = 10
    print(end + next)

    x{end of month}= today
    y{next monday}= today
    print(x)
    print(y)

end:
    print("ok")

    end = 1
    if end = 1 then
        print("works")
    end if

    ' `loop` and `until` both became keywords when `do ... loop` landed, and
    ' both stayed usable as ordinary names because neither could START a
    ' statement. On 2026-08-27 `do ... loop while c` was removed and the
    ' surviving form spelled `do ... until c`, which split them: `loop` stopped
    ' being a keyword at ALL and is a plain identifier again, while `until`
    ' became statement-initial and therefore reserved.
    '
    ' The LABEL case is what this fixture is really for, and it is not
    ' hypothetical: four `loop:`/`goto loop` pairs in stdlib/dates.bas stopped
    ' parsing when the keywords were first added, because `label_statement`
    ' accepted the wider name set while `goto`/`gosub` still demanded a bare
    ' IDENT. `loop` is an ordinary identifier now, so that pair is safe by
    ' construction -- and the test below still proves it end to end.
    loop = 3
    print(loop)
    print(counts_to_two())
end program

function counts_to_two()
    i = 0
loop:
    i = i + 1
    if i < 2 then goto loop
    return i
end function
