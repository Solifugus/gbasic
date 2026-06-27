' Define-and-attach sugar (first_class_functions_design.md Phase 2):
' `function obj.method(...)` is an executable statement that registers an internal
' function and stores a reference to it in obj.method. It is NOT hoisted — obj must
' already exist as a record when the statement runs.

function pct(n, p)
    return n * p / 100
end function

account = { name: "checking", balance: 0 }

function account.deposit(amount)
    this.balance = this.balance + amount
    return this.balance
end function

' A method body may call an ordinary top-level function.
function account.tip()
    return pct(this.balance, 10)
end function

print account.deposit(200)
print account.tip()
print type(account.deposit)

' Attach onto a prototype; a derived instance inherits the method field (PBI copy)
' and binds `this` to itself.
proto = { tag: "P" }
function proto.who()
    return this.tag
end function
inst = new proto
print inst.who()

' Re-attaching replaces the method in place (the statement runs when reached).
function account.deposit(amount)
    this.balance = this.balance + amount * 2
    return this.balance
end function
print account.deposit(10)
