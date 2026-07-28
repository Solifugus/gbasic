' PLAT-STDERR: a child that writes a known, alternating mix to both streams.
'
' The two streams carry disjoint content, so a reader that captures them
' separately can prove nothing crossed: every OUT- line must be on fd 1 and
' every ERR- line on fd 2, with no line appearing on both.
'
' `print to error` is exercised from every statement position it can occupy --
' top level, inside a function, and as an inline-if consequent -- because the
' grammar admits print_statement in three separate statement lists and a form
' that parsed in only one of them would be a latent hole.
function emit_from_function(tag)
    print "OUT-fn-" + tag
    print to error "ERR-fn-" + tag
    return nothing
end function

print "OUT-1"
print to error "ERR-1"
print "OUT-2"
print to error "ERR-2"

emit_from_function("a")

' Inline if: the consequent is a statement in its own grammar list.
n = 3
if n = 3 then print to error "ERR-inline"
if n = 3 then print "OUT-inline"

' Inside a loop body (statement_list), and with a computed expression rather
' than a literal -- `print to error` takes a full expression, exactly as `print`
' does, not a string literal.
i = 1
while i <= 2
    print to error "ERR-loop-" + i
    i = i + 1
end while

print "OUT-last"
