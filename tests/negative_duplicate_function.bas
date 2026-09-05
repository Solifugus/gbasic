' Defining the same function twice in one file. The second used to overwrite
' the first silently, so the program ran the definition the author was less
' likely to be looking at.
function greet(name)
    return "hello " + name
end function

function greet(name)
    return "HELLO " + name
end function

print greet("world")
