' The post-test loop: `do ... loop until c` / `do ... loop while c`.
'
' `while ... end while` tests BEFORE the body, so the "run it at least once,
' then decide" shape had to be written by duplicating the first iteration or by
' priming a flag. This is the only loop form gBASIC was missing after the
' counted `for`; a pre-test `do while ... loop` is deliberately NOT added,
' because `while` already is one.
'
' Note `repeat ... until` is not available and will not be: `repeat` is a string
' builtin, and making it a keyword would break every existing use.

program main(args)
    ' `until` stops when the condition becomes true.
    i = 0
    do
        i = i + 1
    loop until i >= 3
    print "loop until      : i=" + i

    ' `while` continues for as long as it stays true.
    j = 0
    do
        j = j + 1
    loop while j < 3
    print "loop while      : j=" + j

    ' The whole point: the body runs even when the condition is already
    ' satisfied on entry. A `while` here would not have run at all.
    n = 100
    do
        n = n + 1
    loop until true
    print "always runs once: n=" + n

    ' The `while` form, same check.
    m = 100
    do
        m = m + 1
    loop while false
    print "same for while  : m=" + m

    out = ""
    k = 0
    do
        k = k + 1
        if k = 2 then
            continue
        end if
        if k > 4 then
            break
        end if
        out = out + k + " "
    loop until false
    print "break/continue  : " + out

    ' Nested, and nested inside a counted for.
    total = 0
    for a = 1 to 3
        b = 0
        do
            b = b + 1
            total = total + 1
        loop until b >= a
    end for
    print "nested in for   : " + total

    ' `loop` and `until` never start a statement, so they remain ordinary
    ' names -- the same courtesy `end` and `next` already get.
    loop = 2
    until = 3
    print "still names     : " + (loop + until)
end program
