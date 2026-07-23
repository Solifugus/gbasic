' Copy-on-write array semantics (docs/array_cow_design.md). Every case asserts
' that arrays keep VALUE semantics: assignment/argument passing never create
' observable aliasing, and mutating one copy never disturbs another. The COW
' backing store is an invisible optimization — this test would pass identically
' under the old eager-deep-copy implementation.

function read_first(arr)
    return arr[0]
end function

function mutate_first(arr)
    arr[0] = 42
    return arr[0]
end function

function make_array()
    local = [0, 0, 0]
    return local
end function

function join_nums(arr)
    parts = []
    for each n in arr
        parts = append(parts, string(n))
    end for
    return join(parts, ",")
end function

' --- assignment isolation ------------------------------------------------
a = [1, 2, 3]
b = a
b[0] = 99
print "assign: a=" + join_nums(a) + " b=" + join_nums(b)

' --- three aliases, mutate one --------------------------------------------
x = [10, 20, 30]
y = x
z = x
y[1] = 200
print "aliases: x=" + join_nums(x) + " y=" + join_nums(y) + " z=" + join_nums(z)

' --- unique mutation in place ---------------------------------------------
u = [0, 0, 0]
u[0] = 1
u[1] = 2
u[2] = 3
print "unique: u=" + join_nums(u)

' --- nested arrays: b[i][j] = x -------------------------------------------
n1 = [[1, 2], [3, 4]]
n2 = n1
n2[0][0] = 77
print "nested: n1[0][0]=" + string(n1[0][0]) + " n2[0][0]=" + string(n2[0][0])
print "nested deep: n1[1][1]=" + string(n1[1][1]) + " n2[1][1]=" + string(n2[1][1])

' --- arrays of records: b[i].field = x ------------------------------------
r1 = [{v: 1}, {v: 2}]
r2 = r1
r2[0].v = 55
print "arr-of-rec: r1[0].v=" + string(r1[0].v) + " r2[0].v=" + string(r2[0].v)

' --- records containing arrays: r2.rows[i] = x ----------------------------
rec1 = {rows: [1, 2, 3], tag: "t"}
rec2 = rec1
rec2.rows[0] = 88
print "rec-arr: rec1.rows[0]=" + string(rec1.rows[0]) + " rec2.rows[0]=" + string(rec2.rows[0])

' --- function argument: read-only does not copy-observe --------------------
c = [1, 2, 3]
print "arg read: first=" + string(read_first(c)) + " c[0]=" + string(c[0])

' --- function argument: callee mutation does not touch caller -------------
d = [1, 2, 3]
mutated = mutate_first(d)
print "arg mutate: d[0]=" + string(d[0]) + " returned=" + string(mutated)

' --- function return preserves value semantics ----------------------------
made = make_array()
made[0] = 9
again = make_array()
print "return: made[0]=" + string(made[0]) + " fresh[0]=" + string(again[0])

' --- append: bare mutates target, aliased copy stays independent -----------
e = [1, 2]
f = e
append(e, 3)
print "append bare: e=" + join_nums(e) + " f=" + join_nums(f)

' --- append: assigned form, alias taken before append ---------------------
g = [1, 2]
h = g
gg = append(g, 3)
print "append assign: g=" + join_nums(g) + " h=" + join_nums(h) + " gg=" + join_nums(gg)

' --- append growth: build a longer array, values intact -------------------
grow = []
gi = 0
while gi < 10
    grow = append(grow, gi * gi)
    gi = gi + 1
end while
print "grow count=" + string(count(grow)) + " grow[9]=" + string(grow[9]) + " grow[0]=" + string(grow[0])

' --- insert / remove keep value semantics ---------------------------------
iv = [1, 2, 4]
ivc = iv
insert(iv, 2, 3)
print "insert: iv=" + join_nums(iv) + " ivc=" + join_nums(ivc)
rm = [1, 2, 3, 4]
rmc = rm
remove(rm, 1)
print "remove: rm=" + join_nums(rm) + " rmc=" + join_nums(rmc)

' --- sort / reverse / unique keep value semantics -------------------------
sv = [3, 1, 2]
svc = sv
sort(sv)
print "sort: sv=" + join_nums(sv) + " svc=" + join_nums(svc)
rv = [1, 2, 3]
rvc = rv
reverse(rv)
print "reverse: rv=" + join_nums(rv) + " rvc=" + join_nums(rvc)

' --- iteration is unchanged and does not mutate the source ----------------
it = [{v: 1}, {v: 2}, {v: 3}]
sum = 0
for each item in it
    item.v = item.v + 100
    sum = sum + item.v
end for
print "iter: sum=" + string(sum) + " it[0].v=" + string(it[0].v)

' --- equality is structural, independent of sharing -----------------------
eqa = [1, 2, 3]
eqb = [1, 2, 3]
eqc = eqa
print "equal: distinct=" + string(eqa = eqb) + " aliased=" + string(eqa = eqc)

' --- serialization round-trips through an independent store ---------------
ser = serialize([1, [2, 3], {k: 4}])
back = deserialize(ser)
back[0] = 100
print "ser: count=" + string(count(back)) + " nested=" + string(back[1][1]) + " rec=" + string(back[2].k)
