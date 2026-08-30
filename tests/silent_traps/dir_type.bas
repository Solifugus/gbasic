' A wrong-typed argument must RAISE, not print and return `nothing`.
'
' This fixture used to pass a string, which was refused. A plain string path
' is legal now -- `list`/`files`/`folders` take one the way `list_files`
' always has -- so the example moved to an array, which cannot name a
' directory under any reading. The property under test is unchanged: a wrong
' type is a located, catchable raise with a nonzero exit.
r = files([1, 2])
print("unreachable")
