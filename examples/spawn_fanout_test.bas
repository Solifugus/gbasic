' Multiprocessing Phase 1c: fan out work to several actors and sum the replies
' (docs/multiprocessing_design.md §8). Each child squares its number and sends
' the result to the parent. The test is order-independent by construction -- the
' sum is commutative -- so scheduling nondeterminism cannot flake it.
function squarer(parent, n)
    send(parent, n * n)
end function

program main(args)
    me = self()
    a = spawn squarer(me, 3)
    b = spawn squarer(me, 4)
    c = spawn squarer(me, 5)
    total = receive() + receive() + receive()
    print(total)
end program
