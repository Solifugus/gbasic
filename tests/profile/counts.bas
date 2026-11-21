' tests/run_profile.sh's counts fixture. Every place the profiler counts keeps
' its OWN counter in `tally`, and the program prints them as `oracle NAME N`;
' the runner holds the profile's figures to those, never to each other.
' `tally` is main's record, and a function writes its fields (a function cannot
' rebind main's variable, and a parameter would be a copy). The library's
' calls are counted where they are made.
load countlib

function fib(n)
    tally.fib += 1
    if n < 2 then return n
    return fib(n - 1) + fib(n - 2)
end function

function seven()
    tally.seven += 1
    return 7
end function

' Called only from countlib: its first call is made while the library's file
' is the one running, and the report must still put it in this file.
function called_back()
    tally.back += 1
    return 1
end function

program main(args)
    tally = { fib: 0, seven: 0, triple: 0, body: 0, watched: 0, oneline: 0, back: 0 }
    for i = 1 to 1000
        tally.body += 1
    next
    for i = 1 to 37
        x = seven()
    next
    f = fib(15)
    for i = 1 to 11
        x = countlib.triple(i)
        tally.triple += 1
    next
    for i = 1 to 4
        x = countlib.call_back(called_back)
    next
    w = 0
    watch(w)
        tally.watched += 1
    end watch
    for i = 1 to 9
        w = i
    next
    for i = 1 to 20
        if i > 5 then tally.oneline += 1
    next
    print "oracle fib " + string(tally.fib)
    print "oracle seven " + string(tally.seven)
    print "oracle countlib.triple " + string(tally.triple)
    print "oracle called_back " + string(tally.back)
    print "oracle body " + string(tally.body)
    print "oracle watched " + string(tally.watched)
    print "oracle oneline_reached 20"
end program
