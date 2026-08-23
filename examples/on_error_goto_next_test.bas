' `on error goto next` semantics, proven behaviors (docs/error_model_design.md).
' The handler is FRAME-scoped: it governs the frame that executed it and no
' other. A raise there abandons the statement, records the error, and execution
' continues at the next statement in the same list.
'
' Case (3) is where this differs from the deleted `on error resume next`, and
' the difference is the point of the redesign. Under the old process-global
' mode a callee "resumed locally" and returned a value, yet the CALLER's
' assignment was abandoned anyway by the generation check -- which is why a
' function could never catch a raise and return a fallback. Now an unarmed
' callee simply propagates: its frame is destroyed, not resumed, and the raise
' is absorbed by the first armed frame -- here, the top level.

function unarmed()
    y = number("abc")            ' raises; this frame is NOT armed, so it
    print("3a: NOT PRINTED")     '   propagates and the frame is destroyed
    return "returned"
end function

function armed()
    on error goto next           ' this frame arms itself
    y = number("abc")
    if error then
        return "fallback"        ' catch-and-return: impossible before
    end if
    return "unreached"
end function

on error goto next

' (1) Statement-list fall-through, and the error is readable.
'     NOTE the shape: only BARE `error` acknowledges. Reading `error.source`
'     alone would leave the error pending, and the next raise would then
'     escape this frame under rule 1 rather than being absorbed -- which is
'     the design refusing to let a second failure quietly shadow the first.
x = number("abc")
if error then
    print("1: fell through; source=" + error.source)
end if

' (2) The raising assignment does not write its target; it keeps its prior
'     value. Assign the fallback inside the check block.
n = 5
n = number("abc")
if error then
    print("2: n still " + string(n))
end if

' (3) An unarmed callee propagates: the caller's assignment is abandoned and
'     the top frame absorbs the raise.
r = "initial"
r = unarmed()
if error then
    print("3b: r still " + r)
end if

' (4) An armed callee absorbs its own raise and returns normally. The caller
'     sees an ordinary return -- no error reaches this frame at all.
v = armed()
if error then
    print("4: NOT PRINTED -- the callee handled it")
end if
print("4: armed() returned " + v)
