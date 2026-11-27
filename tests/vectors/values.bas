' docs/vectors_design.md, increment 1: the vector value, its arithmetic and
' its lock. Every line of values.out was worked out by hand from the design,
' with numbers chosen so the answers are exact (3-4-5 triangles, unit axes).
' The oracle tier (tests/vectors/oracle.py) holds the arithmetic to Python on
' random vectors; this holds the meaning.

shape chunk
    body:text
    embedding:vector(3)
end shape

function midpoint(a:vector(2), b:vector(2)):vector(2)
    return (a + b) / 2
end function

function echo_back(parent, v)
    send(parent, v)
end function

program main(args)
    ' -- making one, and back (§2)
    a = vector([1, 2, 3])
    b = vector("[4, 5, 6]")
    print a
    print b
    print type(a)
    print vector(a) = a
    print array(a)
    print vector([0.1])
    print vector([0.1])[0]
    print vector([-0.0, 16777217])
    print len(vector_bytes(a))
    print vector_from_bytes(vector_bytes(a)) = a
    print vector_from_bytes(hex_decode("0000c03f000000c0"))

    ' -- reading one (§3)
    print len(a)
    print a[0] + a[2]
    total = 0
    for each x in a
        total = total + x
    next
    print total
    for each x, i in b
        print string(i) + ":" + x
    next
    print a = vector([1, 2, 3])
    print a = vector([1, 2])
    print a != b
    print a = [1, 2, 3]
    print vector([0]) = vector([-0.0])
    print [a, "x"]
    print "a is " + a
    print json_encode({ e: vector([0.5, -1]) })
    print contains([b, a], vector([1, 2, 3]))
    print unique([a, b, vector([1, 2, 3])])

    ' -- arithmetic (§4)
    print dot(a, b)
    print norm(vector([3, 4]))
    print norm(vector([1, 2, 2]))
    print cosine(vector([1, 0]), vector([0, 1]))
    print cosine(vector([1, 0]), vector([5, 0]))
    print cosine(vector([1, 0]), vector([-3, 0]))
    print distance(vector([0, 0]), vector([3, 4]))
    print distance(vector([1, 0]), vector([0, 1]), "cosine")
    print distance(vector([0, 2]), vector([0, 7]), "cosine")
    print distance(a, b, "ip")
    print distance(a, b, "l2") = distance(a, b)
    print normalize(vector([3, 4]))
    print normalize(vector([0, -2]))
    print a + b
    print b - a
    print a * 2
    print 0.5 * a
    print a / 4
    print -1 * a
    print midpoint([0, 0], [2, 4])

    ' nearest: four points around the origin, a tie between 1 and 2 at
    ' distance 1 broken by the lower index
    points = [vector([3, 4]), vector([1, 0]), vector([0, 1]), vector([-2, 0])]
    print nearest(vector([0, 0]), points, 3)
    print nearest(vector([0, 0]), points, 0)
    print nearest(vector([0, 0]), points, 9)
    print nearest(vector([1, 0]), points, 2, "cosine")
    print nearest(vector([1, 0]), points, 2, "ip")
    print nearest(vector([1, 0]), [], 3)

    ' -- the lock (§5)
    e:vector(3) = [1, 2, 3]
    print e
    e = "[0.25, 0.5, 0.75]"
    print e
    any:vector = [7]
    print any
    any = [1, 2, 3, 4, 5]
    print len(any)
    for each v:vector in [[1, 2], "[3]"]
        print v
    next
    c = new chunk with { body: "hi", embedding: [0.5, 0, 1] }
    print c.embedding
    print type(c.embedding)
    print json_encode(json_schema(chunk).properties.embedding)
    plain = { body: "x", embedding: vector([1, 1, 1]) }
    shaped:chunk = plain
    print shaped.embedding = vector([1, 1, 1])

    ' -- across a process (§6)
    print deserialize(serialize(a)) = a
    print type(deserialize(serialize([a, b]))[1])
    w = spawn echo_back(self(), { e: vector([1.5, 2.5]) })
    r = receive()
    print r.e
    print type(r.e)
end program
