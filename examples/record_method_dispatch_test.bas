' Record property read/write, nested record access, and a record method with
' an implicit `this`. Kept from the gi bridge's regression suite when GTK and
' gi were removed: the record path it guards is ordinary language behaviour.
function greet()
    return "hi " + this.name
end function

person = { name: "ada", address: { city: "london" } }
print person.name
print person.address.city

person.name = "grace"
person.address.city = "baltimore"
print person.name
print person.address.city

person.hello = greet
print person.hello()
