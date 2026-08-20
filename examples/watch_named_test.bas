' Named, first-class watchers (agreed 2026-08-20): `watch name(targets)` binds
' `name` to a watcher VALUE in scope; `unwatch <expr>` turns that watcher off;
' `watchers()` lists the live handles; `.name`/`.targets` identify one.
'
' The load-bearing rules under test, each the answer to a failure mode:
'   * REPLACE-ON-REDECLARE -- re-running setup code must not stack a second
'     watcher doing the work twice. Proven by arithmetic: the old body adds 1,
'     the new adds 100, so one change after redeclare moves the counter by
'     exactly 100 (a stacked pair would move it by 101).
'   * unwatch on an already-off handle is a QUIET NO-OP -- the watcher is
'     definitively off either way, so the caller's belief is true.
'   * the anonymous form and `without watchers` are unchanged.

a = 10
b = 0

watch doubler(a)
    b = a * 2
end watch

print b
a = 15
print b

' --- the handle is a value: fields, display, storage ---
print doubler.name
print doubler.targets
print doubler
box = { w: doubler }
print box.w.name

' --- watchers() lists live handles ---
ws = watchers()
print count(ws)
print ws[0].name

' --- unwatch stops firing; a dead handle unwatches as a no-op ---
old = doubler
unwatch doubler
a = 50
print b
unwatch old
print b

' --- redeclare after unwatch: setup is safe to re-run ---
hits = 0
x = 1
watch counter(x)
    hits = hits + 1
end watch
x = 2
print hits

' --- replace-on-redeclare: exactly one watcher remains ---
watch counter(x)
    hits = hits + 100
end watch
x = 3
print hits
print count(watchers())

' --- anonymous watchers still work beside named ones ---
c = 0
watch(x)
    c = x
end watch
x = 7
print c
print hits

' --- without watchers still suppresses named watchers ---
without watchers
    x = 9
end without
print hits
print c

' --- identity, not structure: copies of one handle are equal, two
' registrations never are (the PLAT-EQ lesson, pinned for watchers) ---
w2 = counter
print counter = w2
print old = counter
print old != counter
print counter = 5
