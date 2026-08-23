' reflect — general runtime reflection (NAP-9). Enumerate the current scope's
' variables and describe/traverse any value with small composable primitives,
' without serializing it. Reflectable is broader than serializable.

num = 42
str = "hi"
flag = true
none = nothing
unk = unknown
arr = [10, 20, 30]
rec = { alpha: 1, beta: { deep: 2 }, gamma: [true, false] }

' -- variable enumeration (current scope, sorted, deterministic) --
print "== variables =="
vars = reflect.variables()
for each v in vars
    print v
end for

' -- scalar inspection: kind / category / serializable --
print "== scalars =="
print reflect.kind(num) + " " + reflect.category(num) + " ser=" + reflect.serializable(num)
print reflect.kind(str) + " count=" + reflect.count(str)
print reflect.kind(flag)
print reflect.kind(none)
print reflect.kind(unk)

' -- reflect.get retrieves a variable by name --
print "== get =="
print reflect.get("num")
print reflect.kind(reflect.get("rec"))

' -- record traversal: fields / field / nested --
print "== record =="
print "count=" + reflect.count(rec)
for each f in reflect.fields(rec)
    print f
end for
beta = reflect.field(rec, "beta")
print "beta.count=" + reflect.count(beta)
print reflect.field(beta, "deep")

' -- array traversal: count / element / nested --
print "== array =="
print "count=" + reflect.count(arr)
print reflect.element(arr, 0)
print reflect.element(arr, 2)
gamma = reflect.field(rec, "gamma")
print reflect.kind(gamma) + " count=" + reflect.count(gamma)
print reflect.element(gamma, 1)

' -- inspect: a shallow descriptor (kind/type/category/serializable/count) --
print "== inspect =="
info = reflect.inspect(rec)
print info.kind + " " + info.type + " " + info.category + " ser=" + info.serializable + " count=" + info.count
si = reflect.inspect(str)
print si.kind + " " + si.category + " count=" + si.count

' -- serializable is recursive; pure data is serializable --
print "== serializable =="
print reflect.serializable(rec)
print reflect.serializable(arr)

' -- cycle safety: gBASIC copy semantics prevent true cycles; reflection is safe --
print "== cycle =="
cyc = {}
cyc.self = cyc
print reflect.kind(reflect.field(cyc, "self"))
print reflect.serializable(cyc)

' -- reflection is recoverable: an unknown variable raises, we continue --
print "== recover =="
on error goto next
bad = reflect.get("does_not_exist")
if error then
    print "true"
end if
print "continued"
