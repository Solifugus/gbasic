' docs/shapes_design.md increment 1: the block, instances, fixed fields,
' writes and methods. Every expected line in instances.out was worked out by
' hand from the design, before the implementation existed.
ids = { n: 0 }
function next_id()
    ids.n += 1
    return ids.n
end function

shape person
    name:text = "nobody"
    born:date
end shape

shape label
    text:text = ""
    size:number = 1
end shape

shape account
    owner:text = "unnamed"
    balance:money(USD) = "0.00"
    opened:date
    tags = []
    holder:person
    id (reset next_id()):number
    function deposit(amount)
        this.balance += amount
        return this.balance
    end function
    function describe()
        return this.owner + " has " + string(this.balance)
    end function
end shape

' the prototype is the shape's name, holding the defaults
print account.owner
print account.balance
print is_nothing(account.opened)

' new and with: defaults, overrides converted, reset re-fired
a = new account with { owner: "Ann", balance: "12.50" }
print a.owner
print a.balance
print a.id
b = new account
print b.id
print b.owner

' a write converts, as a locked variable's does
a.balance = "1.25"
print a.balance
a.opened = "2026-11-02"
print a.opened
a.tags = append(a.tags, "vip")
print a.tags

' methods, through the shape
print a.deposit("2.00")
print a.describe()

' a shaped field: a plain record converts into it, field by field
a.holder = { name: "Ann", born: "1990-05-01" }
print a.holder.name
print a.holder.born
a.holder.name = "Ann B"
print a.holder.name

' copies are values: a copy keeps the shape and changes alone
c = a
c.owner = "Cy"
print a.owner
print c.owner

' equality is by fields; the shape and its methods are not compared
p = new person
print p = { name: "nobody", born: nothing }
print has(a, "deposit")
print count(keys(a))

' what leaves the program is plain data
print json_encode(new label with { text: "Bo", size: "3" })
m = merge(new person, { extra: 1 })
m.other = 2
print m.other
