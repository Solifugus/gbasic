' PBI Phase 2: `new` derives an instance and the four policies take effect.
seed = 5
account = {
    owner (copy): "proto",
    bank (link): "Shared Bank",
    id (reset seed * 2): 0,
    scratch (exclude): "temp"
}

a = new account
seed = 7
b = new account

' reset re-evaluates each `new` (global scope); the prototype keeps its literal
print a.id
print b.id
print account.id

' copy is independent
a.owner = "Ada"
print a.owner
print b.owner

' exclude omits the field on instances
print has(a, "scratch")
print has(account, "scratch")

' link writes through to the prototype and siblings
a.bank = "Changed Bank"
print account.bank
print b.bank

' with overrides (wins over reset) and adds a field
d = new account with { id: 42, label: "custom" }
print d.id
print d.label

' recursive derivation re-derives nested instances (distinct serials)
seed = 100
engine = { serial (reset seed): 0 }
car = { motor (copy): new engine }
seed = 200
c1 = new car
seed = 300
c2 = new car
print c1.motor.serial
print c2.motor.serial
print car.motor.serial
