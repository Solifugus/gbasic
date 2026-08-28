' The post-test loop: `do ... until c`.
'
' `while ... end while` tests BEFORE the body, so the "run it at least once,
' then decide" shape had to be written by duplicating the first iteration or by
' priming a flag. This is the only loop form gBASIC was missing after the
' counted `for`; a pre-test `do while ... loop` is deliberately NOT added,
' because `while` already is one.
'
' ONE FORM, AND `until` IS A STOP CONDITION. A continue-condition spelling
' (`do ... loop while c`) existed until 2026-08-27 and was removed. It was
' redundant -- it is `until not c`, and `!<`/`!>` cover the single-comparison
' case -- and it was what FORCED the `loop` keyword: `do ... while c` cannot be
' told from a body whose next statement is a nested `while c ... end while`,
' because both readings are complete programs and the `end while` that
' separates them can be arbitrarily far ahead. `until` never begins a
' statement, so it needs nothing in front of it.
'
' Note `repeat ... until` is not available and will not be: `repeat` is a string
' builtin, and making it a keyword would break every existing use.

program main(args)
    ' `until` stops when the condition becomes true.
    i = 0
    do
        i = i + 1
    until i >= 3
    print "until           : i=" + i

    ' The continue-condition shape, written the way it is written now. The old
    ' `loop while j < 3` is this, and `!<` keeps it a single comparison rather
    ' than forcing a `not`.
    j = 0
    do
        j = j + 1
    until j !< 3
    print "negated compare : j=" + j

    ' A compound condition is where negation actually costs something: this is
    ' the old `loop while a < 3 and b < 3`, and De Morgan is NOT required --
    ' `not` around the original reads the same as it always did.
    a = 0
    b = 0
    do
        a = a + 1
        b = b + 2
    until not (a < 3 and b < 3)
    print "compound        : a=" + a + " b=" + b

    ' The whole point: the body runs even when the condition is already
    ' satisfied on entry. A `while` here would not have run at all.
    n = 100
    do
        n = n + 1
    until true
    print "always runs once: n=" + n

    ' ...and the never-stops-early case, the old `loop while false`.
    m = 100
    do
        m = m + 1
    until not false
    print "same, negated   : m=" + m

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
    until false
    print "break/continue  : " + out

    ' Nested, and nested inside a counted for.
    total = 0
    for a = 1 to 3
        b = 0
        do
            b = b + 1
            total = total + 1
        until b >= a
    end for
    print "nested in for   : " + total

    ' `loop` is no longer a keyword in ANY position -- dropping the
    ' continue-condition form is what freed it -- so it is an ordinary name.
    ' `until` is the one that had to become reserved in exchange: it is now
    ' statement-initial, so it cannot also be a variable.
    loop = 2
    print "loop is a name  : " + (loop + 3)
end program
