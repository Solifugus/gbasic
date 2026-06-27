' constructor (first_class_functions_design.md Phase 3): a field named `constructor`
' is auto-invoked by `new` after derivation, with `this` = the new instance. Inputs
' arrive through `new … with {…}` and are read from `this`. Its return is ignored.

function constructor()
    this.balance = this.opening
    this.label = "acct:" + this.name
end function

Account = { name: "", opening: 0, balance: 0, label: "", constructor: constructor }

a = new Account with { name: "alice", opening: 100 }
print a.balance
print a.label

b = new Account with { name: "bob", opening: 50 }
print b.balance
print b.label

' Composes with methods: the constructor sets up state the method then uses.
function deposit(amount)
    this.balance = this.balance + amount
    return this.balance
end function
Account2 = { name: "", opening: 0, balance: 0, deposit: deposit, constructor: constructor }
c = new Account2 with { name: "carol", opening: 10 }
print c.deposit(5)

' Composes with PBI: a derived prototype overrides the constructor field.
function base_ctor()
    this.kind = "base"
end function
function derived_ctor()
    this.kind = "derived"
end function
Base = { kind: "", constructor: base_ctor }
print (new Base).kind
Derived = new Base with { constructor: derived_ctor }
print (new Derived).kind

' No constructor: plain new still works.
Plain = { x: 1 }
print (new Plain).x
