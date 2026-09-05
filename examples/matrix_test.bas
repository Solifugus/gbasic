' matrix.bas vector/matrix toolkit (statistics_design.md §8 shared infra).
' Values checked against numpy. Matrices are lists of rows; indices 0-based.
program demo(args)
    load matrix from "../stdlib/matrix.bas"

    a = [[4, 7], [2, 6]]

    print("rows " + string(matrix.mat_rows(a)))
    print("cols " + string(matrix.mat_cols(a)))

    ' Transpose.
    t = matrix.mat_transpose([[1, 2, 3], [4, 5, 6]])
    print("t00 " + string(t[0][0]))
    print("t21 " + string(t[2][1]))

    ' Product (2x2)(2x2).
    prod = matrix.mat_mul([[1, 2], [3, 4]], [[5, 6], [7, 8]])
    print("p00 " + string(prod[0][0]))
    print("p01 " + string(prod[0][1]))
    print("p10 " + string(prod[1][0]))
    print("p11 " + string(prod[1][1]))

    ' Matrix times vector.
    mv = matrix.mat_vec([[1, 2], [3, 4]], [5, 6])
    print("mv0 " + string(mv[0]))
    print("mv1 " + string(mv[1]))

    ' Identity.
    id = matrix.mat_identity(3)
    print("id11 " + string(id[1][1]))
    print("id12 " + string(id[1][2]))

    ' Inverse, and a * inv = I.
    inv = matrix.mat_inverse(a)
    print("inv00 " + string(round(inv[0][0], 6)))
    print("inv01 " + string(round(inv[0][1], 6)))
    print("inv10 " + string(round(inv[1][0], 6)))
    print("inv11 " + string(round(inv[1][1], 6)))
    check = matrix.mat_mul(a, inv)
    print("chk00 " + string(round(check[0][0], 6)))
    print("chk01 " + string(round(check[0][1], 6)))
    print("chk11 " + string(round(check[1][1], 6)))

    ' Dot product.
    print("dot " + string(matrix.vec_dot([1, 2, 3], [4, 5, 6])))

    ' Singular matrix has no inverse.
    print("sing " + string(matrix.mat_inverse([[1, 2], [2, 4]])))
end program
