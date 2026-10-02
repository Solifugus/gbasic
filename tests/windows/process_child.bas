' A child process for tests/windows/process_run.bas -- gBASIC itself, so the
' suite needs nothing installed and runs unchanged on Linux and Windows. The
' first argument picks the behaviour.
program main(args)
    mode = args[0]
    if mode = "args" then
        ' Every argument after the mode, exactly as the child received it.
        rest = []
        i = 1
        while i < count(args)
            append(rest, args[i])
            i += 1
        end while
        print(encode(rest))
    else if mode = "streams" then
        print("out")
        print to error "oops"
    else if mode = "exit" then
        exit(number(args[1]))
    else if mode = "big" then
        ' Both streams at once, far past any pipe buffer: a parent that drains
        ' one stream at a time deadlocks here instead of failing.
        line = repeat("x", 99)
        n = number(args[1])
        for i = 1 to n
            print(line)
            print to error line
        next
    else if mode = "sleep" then
        sleep(number(args[1]))
    else if mode = "cat" then
        f {file}= args[1]
        print(read(f))
    else if mode = "env" then
        print(string(env(args[1])))
    end if
end program
