' User functions called with parameters, from the program and from a library
' function, by a count given on the command line (tests/run_resolve.sh runs it
' at N and 2N): each call binds its parameters, reads them, assigns a local and
' returns. Not a call through a function value (`f = area`, `f(1, 1)`): that
' still resolves its function by the value's name on every call, which
' docs/llvm_backend_design.md records as left.
load resolvelib

function area(w, h)
    a = w * h
    return a
end function

program main()
    n = number(env("RESOLVE_N"))
    total = 0
    for i = 1 to n
        total = total + area(i, 2) + resolvelib.twice(i)
    next
    print total
end program
