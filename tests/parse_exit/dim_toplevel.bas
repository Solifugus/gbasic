' A token the grammar has no place for, at TOP LEVEL — where the synthetic
' end-of-file the lexer interface returns lands somewhere the grammar accepts.
'
' The line before it used to RUN, everything after it silently disappeared, and
' the process exited 0. That is the whole defect in three lines.
print "before"
dim x
print "after"
