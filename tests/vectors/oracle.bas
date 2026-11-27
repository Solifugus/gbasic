' The arithmetic half of run_vectors.sh's oracle tier. Reads the cases
' tests/vectors/oracle.py wrote (args[0]) and prints what gBASIC answers for
' each; oracle.py decides, from Python's own arithmetic, whether it is right.
' Vectors arrive and leave as the hex of their float32 bytes, so nothing is
' lost to printing.
'   pair NAME A B S      -> every two-vector answer for A and B, scale S
'   rank NAME K Q R...   -> nearest(Q, [R...], K) under each metric
program main(args)
    for each line in {file}args[0]
        f = split(line, " ")
        if f[0] = "pair" then
            a = vector_from_bytes(hex_decode(f[2]))
            b = vector_from_bytes(hex_decode(f[3]))
            s = number(f[4])
            out = [f[1], string(dot(a, b)), string(norm(a)), string(norm(b)), string(distance(a, b)),
                   string(distance(a, b, "ip"))]
            if norm(a) > 0 and norm(b) > 0 then
                out = append(out, string(cosine(a, b)))
                out = append(out, string(distance(a, b, "cosine")))
            else
                out = append(out, "-")
                out = append(out, "-")
            end if
            out = append(out, hex_encode(vector_bytes(normalize(a))) if norm(a) > 0 else "-")
            out = append(out, hex_encode(vector_bytes(a + b)))
            out = append(out, hex_encode(vector_bytes(a - b)))
            out = append(out, hex_encode(vector_bytes(a * s)))
            out = append(out, hex_encode(vector_bytes(a / s)))
            out = append(out, string(vector_from_bytes(hex_decode(f[2]))))
            print join(out, " ")
        else
            k = number(f[2])
            q = vector_from_bytes(hex_decode(f[3]))
            rows = []
            for i = 4 to len(f) - 1
                rows = append(rows, vector_from_bytes(hex_decode(f[i])))
            next
            for each m in ["l2", "cosine", "ip"]
                out = [f[1], m]
                for each hit in nearest(q, rows, k, m)
                    out = append(out, string(hit.index) + ":" + hit.distance)
                next
                print join(out, " ")
            next
        end if
    next
end program
