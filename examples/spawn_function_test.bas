' Function values cross an actor boundary by registered name (Phase 4, §10). Every
' actor execs the same program, so a function sent to a child resolves there. A
' record carrying a method sends as data + a function reference; the child binds
' `this` and calls it locally. Single sender (main), so output is deterministic.

function bump(x)
    return x + 1000
end function

function describe()
    return this.name + " has " + string(this.balance)
end function

function worker(parent)
    ' a bare function value, called in this child
    fn = receive()
    send(parent, fn(5))
    ' a record with a method, called in this child
    acct = receive()
    send(parent, acct.describe())
end function

program main(args)
    me = self()
    w = spawn worker(me)
    send(w, bump)
    send(w, { name: "alice", balance: 100, describe: describe })
    print(receive())
    print(receive())
end program
