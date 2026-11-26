' docs/shapes_design.md §7, increment 4: a `.field` site remembers the slot it
' last found its field at, and a write site the shape field it last converted
' by. Both are caches, checked by name on every use, so they can only be
' wrong when ONE site meets records of DIFFERENT layouts. Every check here
' does that, and states its own expected value: a cache that skipped its
' check reads the wrong field and prints a MISMATCH. run_shapes.sh's sites
' tier runs this interpreted and compiled, under valgrind too.
program main(args)
    ' one read site, `r.b`, over every layout
    rows = [{ a: 1, b: 2 }, { b: 3, a: 4 }, { x: 0, y: 0, z: 0, b: 5 }, new pair, new flip, wide(20, 6), { a: 1, b: 2 }]
    want = [2, 3, 5, 10, 20, 6, 2]
    for each r, i in rows
        print check("read " + string(i), read_b(r), want[i])
    end for
    print check("read absent", read_b({ a: 1 }), "unknown record field: b")

    ' one write site, `r.b = v`: the shape's field converts, or the record gains b
    print check("write plain", type_and(write_b({ b: 1, a: 2 }, "7")), "string 7")
    print check("write appended", type_and(write_b({ a: 2 }, "7")), "string 7")
    print check("write pair", type_and(write_b(new pair, "7")), "number 7")
    print check("write flip", type_and(write_b(new flip, "7")), "string 7")
    print check("write pair again", type_and(write_b(new pair, "8")), "number 8")
    print check("write refused", write_b(new pair, "soon"), "refused")
    print check("write wide", type_and(write_b(wide(20, 6), "7")), "string 7")

    ' a compound write reads and writes at one site
    print check("bump plain", bump_b({ a: 1, b: 2 }), 3)
    print check("bump pair", bump_b(new pair), 11)
    print check("bump flip", bump_b(new flip), "201")
    print check("bump wide", bump_b(wide(20, 6)), 7)

    ' the other fields are untouched by a write through a cached slot
    p = write_b(new flip, "x")
    print check("others kept", p.a + " " + string(p.n), "first 5")

    ' `this.b` from one method, on records of two layouts
    print check("this pair", (new pair).twice(), 20)
    print check("this flip", (new flip).twice(), "2020")

    ' a record shared by two variables: the write detaches, the slot holds
    q = new pair
    s = q
    s.b = 99
    print check("detached", string(q.b) + " " + string(s.b), "10 99")
end program

function read_b(r)
    on error goto next
    got = r.b
    e = error
    if e then
        return e.message
    end if
    return got
end function

function write_b(r, v)
    on error goto next
    r.b = v
    e = error
    if e then
        return "refused"
    end if
    return r
end function

function bump_b(r)
    r.b += 1
    return r.b
end function

function type_and(r)
    return type(r.b) + " " + string(r.b)
end function

function wide(n, last)
    r = {}
    for i = 1 to n
        r["f" + string(i)] = i
    end for
    r.b = last
    return r
end function

function check(name, got, want)
    if string(got) = string(want) then
        return name + " ok"
    end if
    return name + " MISMATCH got [" + string(got) + "] want [" + string(want) + "]"
end function

shape pair
    b:number = 10
    a:text = "first"
    function twice()
        return this.b * 2
    end function
end shape

shape flip
    a:text = "first"
    n:number = 5
    b:text = "20"
    function twice()
        return this.b + this.b
    end function
end shape
