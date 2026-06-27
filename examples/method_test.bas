' Methods and `this` (first_class_functions_design.md Phase 1):
' a function value reached through a record field is a method; the object is the
' receiver, bound to `this` at the call site. `this.field = …` writes through and
' honors PBI policies. Dispatch falls out of which function value the field holds.

function deposit(amount)
    this.balance = this.balance + amount
    return this.balance
end function

function describe()
    return this.name + ": " + string(this.balance)
end function

account = { name: "checking", balance: 0, deposit: deposit, describe: describe }
print account.deposit(100)
print account.deposit(50)
print account.describe()
' The mutation persisted to the underlying variable.
print account.balance

' Dynamic dispatch: two records, same field name, different function values.
function loud()
    return upper(this.text)
end function
function soft()
    return lower(this.text)
end function
a = { text: "Hello", say: loud }
b = { text: "Hello", say: soft }
print a.say()
print b.say()

' Nested method calls keep their own receiver.
function inner()
    return this.tag
end function
function outer(other)
    return this.tag + "/" + other.inner()
end function
x = { tag: "X", inner: inner, outer: outer }
y = { tag: "Y", inner: inner }
print x.outer(y)

' `this.field` honors PBI policy: a link field writes through to every alias,
' a copy field stays private to the instance.
function setbox(v)
    this.box = v
end function
shared = { box (link): "old", setbox: setbox }
s1 = new shared
s2 = new shared
s1.setbox("new")
print s1.box
print s2.box

function setname(v)
    this.name = v
end function
proto = { name (copy): "orig", setname: setname }
p1 = new proto
p2 = new proto
p1.setname("changed")
print p1.name
print p2.name
