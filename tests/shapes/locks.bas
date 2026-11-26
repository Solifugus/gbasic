' docs/shapes_design.md increment 2: a shape's name as a lock -- a
' variable, a parameter, a return type, a `for each` element. Every expected
' line in locks.out was worked out by hand from §5. The shapes are declared
' BELOW the program block, which reads them: they are hoisted. Every
' conversion here is given each field with no default, so nothing warns
' (the warnings are run_shapes.sh's own tier).
program main(args)
    ' a variable: text converts into each field, a default fills
    a:account = { owner: "Ann", balance: "12.50", opened: "2026-01-05" }
    print a.balance
    print a.opened
    print a.deposit("2.50")

    ' a later plain `=` converts too, as any lock's does
    a = { owner: "Bo", opened: "2026-02-01" }
    print a.owner + " " + string(a.balance)

    ' a field written through the locked variable converts as the field's lock does
    a.balance = "7"
    print a.balance

    ' equality is by fields, the shape ignored (§4.4)
    print a = { owner: "Bo", balance: {USD}"7", opened: {date}"2026-02-01" }

    ' an instance of the shape passes unchanged, and is a copy
    b = new account with { owner: "Cy", opened: "2026-03-01" }
    c:account = b
    c.owner = "Cz"
    print b.owner + " " + c.owner

    ' a parameter and a return type
    r = post({ owner: "Di", balance: "3", opened: "2026-04-01" })
    print r.by + " " + string(r.amount)
    print r.amount * 2

    ' a parameter's lock stays for the body
    print reopen({ owner: "Ed", balance: "9", opened: "2026-04-02" }, "2026-04-03")

    ' a for each element, and the lock outlives the loop
    for each x:account in [{ owner: "Fay", opened: "2026-05-01" }, { owner: "Gus", balance: "1.5", opened: "2026-05-02" }]
        print x.owner + " " + string(x.balance)
    end for
    x = { owner: "Hal", opened: "2026-06-01" }
    print x.owner + " " + string(x.balance)

    ' a shaped field holding another shape's record converts it as a plain record would
    k:holder = { who: { name: "Ivy", born: "1990-05-01" } }
    print k.who.name
    print k.who.born

    ' absence is legal, as in every lock
    z:account = nothing
    print z

    ' relocking to a data type, as `x:text = ...` always could
    a:text = "now text"
    print a
end program

function post(e:account):receipt
    return { amount: e.balance, by: e.owner }
end function

function reopen(e:account, day)
    e = { owner: e.owner, balance: e.balance, opened: day }
    return e.opened
end function

shape account
    owner:text = "unnamed"
    balance:money(USD) = "0.00"
    opened:date
    function deposit(n)
        this.balance += n
        return this.balance
    end function
end shape

shape receipt
    amount:money(USD)
    by:text
end shape

shape person
    name:text
    born:date
end shape

shape holder
    who:person
end shape
