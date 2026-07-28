' PLAT-STDERR: output written with `print to error` reaches fd 2 and does NOT
' reach fd 1.
'
' process.run captures the two streams separately, so this is a direct read of
' where each byte landed -- no shell redirection, no ordering assumptions, and
' nothing timing-dependent: the child has fully exited before either capture is
' inspected.
r = process.run({ command: "./gbasic", args: ["tests/native_platform/plat_stderr_mixed_child.bas"] })

print "exit=" + r.exit_code

' Every OUT- line on fd 1, every ERR- line on fd 2, and neither stream carrying a
' single line belonging to the other. `find` returns nothing when absent.
print "stdout-has-out=" + (find(r.stdout, "OUT-1") != nothing)
print "stdout-has-err=" + (find(r.stdout, "ERR-") != nothing)
print "stderr-has-err=" + (find(r.stderr, "ERR-1") != nothing)
print "stderr-has-out=" + (find(r.stderr, "OUT-") != nothing)

' Statement position: the function body, the inline-if consequent and the while
' body must each have reached fd 2.
print "stderr-has-fn=" + (find(r.stderr, "ERR-fn-a") != nothing)
print "stderr-has-inline=" + (find(r.stderr, "ERR-inline") != nothing)
print "stderr-has-loop=" + (find(r.stderr, "ERR-loop-2") != nothing)

' ...and the matching stdout forms still work as they always did.
print "stdout-has-fn=" + (find(r.stdout, "OUT-fn-a") != nothing)
print "stdout-has-inline=" + (find(r.stdout, "OUT-inline") != nothing)

print "--- fd 1 ---"
print r.stdout
print "--- fd 2 ---"
print r.stderr
print "--- end ---"
