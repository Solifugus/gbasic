' AN ASSIGNMENT STORES WHAT IT IS GIVEN. "The same value" (the second
' equality) decides only whether a watcher is told. An assignment of a value
' the same as the one it replaced used to keep the OLD one: a record in
' another field order kept its old order, and since the comparison is deep,
' `v = { inner: v }` walked all of v each time (run_same_value's shape tier).

print "-- no watcher --"
r = { a: 1, b: 2 }
r = { b: 2, a: 1 }
print r
h = { k: { a: 1, b: 2 } }
h.k = { b: 2, a: 1 }
print h
h["k"] = { a: 1, b: 2 }
print h
xs = [{ a: 1, b: 2 }]
xs[0] = { b: 2, a: 1 }
print xs

print "-- a watcher: told only of a change, the new value stored either way --"
w = { a: 1, b: 2 }
watch w
    print "  told: " + string(w)
end watch
w = { b: 2, a: 1 }
print w
w = { b: 3, a: 1 }
print w

print "-- an equal write to an array shared by copy leaves the other alone --"
p = [1, 2]
q = p
q[0] = 1
q[1] = 5
print p
print q
