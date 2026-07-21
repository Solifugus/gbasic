' NAP-5 regression: ordinary gBASIC record property read/write, nested record
' access, and a record method (implicit this-binding) must behave exactly as
' before. The new per-kind dispatch must never intercept a VALUE_RECORD receiver.
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
