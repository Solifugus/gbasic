' Function values are serializable by registered name (first_class_functions_design
' §10, Phase 4). serialize/deserialize round-trips within one program: the name is
' written, and the receiver resolves it through its own function registry.

function greet(name)
    return "hi " + name
end function

g = greet
s = serialize(g)
h = deserialize(s)
print type(h)
print h("bob")
print h = greet

' A record carrying a method round-trips too: the method field is a function value.
function describe()
    return this.name + " has " + string(this.balance)
end function
account = { name: "alice", balance: 100, describe: describe }
copy = deserialize(serialize(account))
print copy.describe()
