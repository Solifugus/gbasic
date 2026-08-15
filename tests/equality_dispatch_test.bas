' PLAT-EQ, the two places the comparison is used implicitly rather than written.
' Golden-compared by tests/run_equality.sh. Top level rather than inside a
' `program` block because `consider` and `watch` are statements.
'
' These matter more than the operator itself. Nobody writes `if recA = recB`
' very often, but `consider` on a record is ordinary dispatch, and a watcher
' deciding whether a value changed runs on every assignment. Before the fix,
' `consider` matched its FIRST branch whatever the subject was.

print("-- consider dispatches on a record's shape --")
subject = { kind: "circle", r: 2 }
consider subject
if { kind: "square", side: 1 } then
    print("  WRONG: matched square")
if { kind: "circle", r: 3 } then
    print("  WRONG: matched circle with the wrong radius")
if { kind: "circle", r: 2 } then
    print("  right: matched circle r2")
else
    print("  WRONG: fell through to else")
end consider

print("-- consider reaches else when nothing matches --")
consider { kind: "hexagon" }
if { kind: "square" } then
    print("  WRONG: matched square")
if { kind: "circle" } then
    print("  WRONG: matched circle")
else
    print("  right: fell through to else")
end consider

print("-- consider dispatches on an array --")
lst = [1, 2]
consider lst
if [9, 9] then
    print("  WRONG: matched [9,9]")
if [1, 2, 3] then
    print("  WRONG: matched a longer array")
if [1, 2] then
    print("  right: matched [1,2]")
else
    print("  WRONG: fell through to else")
end consider

print("-- consider on a scalar is unchanged --")
consider "look"
if "go" then
    print("  WRONG: matched go")
if "look" then
    print("  right: matched look")
else
    print("  WRONG: fell through to else")
end consider

' A watcher fires when a value CHANGES, which is the same question `=` answers
' and has always been decided by value_storage_equal. Now that `=` delegates to
' that same function, the two cannot disagree -- and this asserts it, because a
' program where `a = b` is true while a watcher reports a change would be
' incoherent. The `watch` statement itself fires once when declared.
print("-- a watcher agrees with the operator about what changed --")
r = { a: 1, b: [2, 3] }
watch r
    print("  FIRED")
end watch
print("  assigning an EQUAL record (must not fire)")
r = { a: 1, b: [2, 3] }
print("  assigning the same fields in another order (must not fire)")
r = { b: [2, 3], a: 1 }
print("  assigning a DIFFERENT nested value (must fire)")
r = { a: 1, b: [2, 9] }
print("  assigning a DIFFERENT shape (must fire)")
r = { a: 1 }
print("  done")

print("-- the same for an array --")
xs = [1, 2, 3]
watch xs
    print("  FIRED xs")
end watch
print("  assigning an equal array (must not fire)")
xs = [1, 2, 3]
print("  assigning a longer array (must fire)")
xs = [1, 2, 3, 4]
print("  done")
