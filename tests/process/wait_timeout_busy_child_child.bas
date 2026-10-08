' The child for wait_timeout_busy_child.bas: either a sleeper, or a producer that
' writes `n` 100-byte lines to BOTH streams at once. A parent draining one stream
' at a time deadlocks on this rather than failing, which is why both are written.
me = process.self()
mode = me.args[0]
if mode = "sleep" then
    sleep(2)
else
    line = repeat("x", 99)
    n = number(mode)
    for i = 1 to n
        print(line)
        print to error line
    next
end if
