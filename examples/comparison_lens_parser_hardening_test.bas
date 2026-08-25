modifier lowered for compare
    return compare(lower(left), operator, lower(right))
end modifier

name = "Bob"
age_text = "18"
age{number}= age_text

if number(age_text) >= 18 then
    print("number call")
end if

if len(name) > 0 then
    print("len call")
end if

if name {caseless}= "bob" then
    print("brace lens")
end if

if name{lowered}= "bob" then
    print("old comparison modifier")
end if

if age >= 18 then
    print("assignment modifier")
end if
