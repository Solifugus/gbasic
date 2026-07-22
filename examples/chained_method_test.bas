' Chained method receivers: the receiver of a method call may itself be a
' field-access, index, or call expression (a.b.method(), a[0].method(),
' make().method()), not only a single variable. The receiver is evaluated once
' and dispatched by runtime kind.

function area()
    return this.w * this.h
end function

function label()
    ' also exercises this.method() inside a chained-receiver method
    return this.tag + "=" + string(this.area())
end function

' field-access receiver: outer.inner.method()
outer = { inner: { w: 4, h: 3, tag: "r", area: area, label: label } }
print outer.inner.area()
print outer.inner.label()

' array-index receiver: a[0].method()
boxes = [ { w: 2, h: 5, tag: "a", area: area, label: label } ]
print boxes[0].area()

' call-result receiver: make().method()
function make_box(n)
    return { w: n, h: n, tag: "sq", area: area, label: label }
end function
print make_box(6).area()

' fully chained on a call result: make().label() calls this.area() through this
print make_box(3).label()

' single evaluation: a side-effecting receiver runs exactly once
counter = { n: 0 }
function bump()
    counter.n = counter.n + 1
    return { w: 10, h: 1, tag: "x", area: area, label: label }
end function
print bump().area()
print counter.n
