' The CONTROL. Every tier above is satisfied by a lexer that suppressed
' newlines unconditionally, so this one asserts the opposite half: outside
' brackets a newline still ends the statement, and the depth returns to zero
' when the brackets close.
'
' Each line here is a separate statement with no terminator but the newline.
' If a closing bracket failed to decrement the depth, everything after the
' first one on this page would be swallowed into a single statement and the
' file would not parse at all.

function foo(n)
    return n
end function

x = foo(1)
y = [2, 3]
z = {a: 4}
print string(x + y[0] + z.a)

' A bracket inside a STRING is text, not depth. A lexer counting characters
' rather than tokens gets this wrong and swallows the rest of the file.
opener = "("
closer = ")"
print opener + closer

' A bracket inside a COMMENT is likewise not depth.  ( [ {
print "comment brackets did not open anything"
