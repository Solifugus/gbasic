' PBI Phase 3: copy is copy-on-write. Copies share storage until written, then
' fork — observably identical to an eager deep copy. Each case below exercises a
' distinct fork-on-write path.

base = { name (copy): "base", tags (copy): ["x"], inner (copy): { n (copy): 1 } }

' 1. assignment aliases the cells; a scalar write forks (record_set barrier)
a = base
a.name = "a"
print base.name
print a.name

' 2. array field: append mutates in place through resolve_lvalue_ref → forks
b = base
append(b.tags, "y")
print len(base.tags)
print len(b.tags)

' 3. nested record: writing one level deep forks level by level
c = base
c.inner.n = 99
print base.inner.n
print c.inner.n

' 4. index-form field assignment also forks
d = base
d["name"] = "d"
print base.name
print d.name

' 5. symmetric fork: mutating the prototype after instances exist leaves them be
proto = { v (copy): "orig" }
i1 = new proto
proto.v = "changed proto"
print i1.v
print proto.v

' 6. link is exempt from COW: writes still go through to every alias
shared = { box (link): "old" }
s1 = new shared
s1.box = "new"
print shared.box
print s1.box
