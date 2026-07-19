' `on error resume next` semantics, proven behaviors (docs/ai/ERRORS.md).
' Once set, the mode is process-global: a raise no longer stops the program;
' it sets error state and execution continues at the NEXT statement in the same
' statement list, while the statement that raised does not complete its value.

function callee()
    y = number("abc")            ' raises; abandoned, then callee resumes locally
    print("3a: callee resumed past its own raise")
    return "returned"
end function

on error resume next

' (1) Statement-list resume, and error state is set.
x = number("abc")
print("1: resumed; error=" + string(error) + " source=" + error.source)

' (2) The raising assignment does not write its target; it keeps its prior value.
n = 5
n = number("abc")
print("2: n still " + string(n))

' (3) The enclosing assignment is abandoned even though the callee resumes and
'     returns a value: the raise inside callee() propagates through the call.
error.clear()
r = "initial"
r = callee()
print("3b: r still " + r)
