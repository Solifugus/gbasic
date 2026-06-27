' First-class function values (first_class_functions_design.md Phase 0):
' a bare function name is a value; it can be stored, passed, compared, and called.

function greet(name)
    return "hello, " + name
end function

function double(n)
    return n * 2
end function

' A bare name evaluates to a function value.
g = greet
print type(g)
print g("world")

' Stored in another variable and called.
d = double
print d(21)

' Equality is same-reference; a function is truthy.
print g = greet
print g = double
print g != double

' Debug representation.
print g
print string(double)

' Passed as an argument and called inside the callee.
function apply_twice(f, x)
    return f(f(x))
end function
print apply_twice(double, 5)

' Carried through arrays and records as plain data.
arr = [greet, double]
print type(arr[0])
rec = { fn: double }
print rec.fn
