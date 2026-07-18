name = "Bob"
age_text = "18"

if len(name) > 0 then
    print("len call")
end if

if starts_with(name, "B") then
    print("starts_with call")
end if

if number(age_text) >= 18 then
    print("number call")
end if

modifier lowered for compare
    return compare(lower(left), operator, lower(right))
end modifier

if name(lowered)= "bob" then
    print("lowered modifier")
end if

function overlap(value)
    return value
end function

modifier overlap for compare
    return compare(left, operator, right)
end modifier

if overlap(name) = "Bob" then
    print("overlap function")
end if

if name(overlap)= "Bob" then
    print("overlap modifier")
end if
