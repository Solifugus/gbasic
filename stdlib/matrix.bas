' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' matrix.bas — minimal vector/matrix toolkit (statistics_design.md §8 shared
' infrastructure). A matrix is a list of rows; each row is a list of numbers.
' A vector is a list of numbers. Indices are 0-based.
'
' Deliberately plain gBASIC (the "compositions in gBASIC" rule); this is the
' component most likely to earn C builtins later if profiling demands it.
' Shape mismatches and singular/non-square matrices return `unknown`.
library matrix
    function mat_rows(a)
        return len(a)
    end function

    function mat_cols(a)
        if len(a) = 0 then
            return 0
        end if
        return len(a[0])
    end function

    ' Transpose an (m x n) matrix to (n x m).
    function mat_transpose(a)
        m = len(a)
        if m = 0 then
            return []
        end if
        n = len(a[0])
        result = []
        j = 0
        while j < n
            row = []
            i = 0
            while i < m
                append(row, a[i][j])
                i = i + 1
            end while
            append(result, row)
            j = j + 1
        end while
        return result
    end function

    ' Matrix product: (m x k) times (k x n) -> (m x n).
    function mat_mul(a, b)
        m = len(a)
        if m = 0 then
            return []
        end if
        k = len(a[0])
        if len(b) != k then
            return unknown
        end if
        n = len(b[0])
        result = []
        i = 0
        while i < m
            row = []
            j = 0
            while j < n
                s = 0
                t = 0
                while t < k
                    s = s + a[i][t] * b[t][j]
                    t = t + 1
                end while
                append(row, s)
                j = j + 1
            end while
            append(result, row)
            i = i + 1
        end while
        return result
    end function

    ' Matrix times vector: (m x n) times (n) -> (m).
    function mat_vec(a, v)
        m = len(a)
        if m = 0 then
            return []
        end if
        n = len(a[0])
        if len(v) != n then
            return unknown
        end if
        result = []
        i = 0
        while i < m
            s = 0
            j = 0
            while j < n
                s = s + a[i][j] * v[j]
                j = j + 1
            end while
            append(result, s)
            i = i + 1
        end while
        return result
    end function

    ' n x n identity matrix.
    function mat_identity(n)
        result = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < n
                if i = j then
                    append(row, 1)
                else
                    append(row, 0)
                end if
                j = j + 1
            end while
            append(result, row)
            i = i + 1
        end while
        return result
    end function

    ' Inverse of a square matrix by Gauss-Jordan elimination with partial
    ' pivoting. Returns unknown if the matrix is singular or non-square. The
    ' input is left untouched (it works on an augmented copy).
    function mat_inverse(a)
        n = len(a)
        if n = 0 then
            return unknown
        end if
        if len(a[0]) != n then
            return unknown
        end if

        ' Augmented working copy [a | I].
        aug = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < n
                append(row, a[i][j])
                j = j + 1
            end while
            j = 0
            while j < n
                if i = j then
                    append(row, 1)
                else
                    append(row, 0)
                end if
                j = j + 1
            end while
            append(aug, row)
            i = i + 1
        end while

        col = 0
        while col < n
            ' Partial pivot: the row at or below col with the largest |entry|.
            pivot = col
            best = abs(aug[col][col])
            r = col + 1
            while r < n
                if abs(aug[r][col]) > best then
                    best = abs(aug[r][col])
                    pivot = r
                end if
                r = r + 1
            end while
            if best = 0 then
                return unknown
            end if
            if pivot != col then
                tmp = aug[col]
                aug[col] = aug[pivot]
                aug[pivot] = tmp
            end if
            ' Scale the pivot row so aug[col][col] = 1.
            pv = aug[col][col]
            j = 0
            while j < 2 * n
                aug[col][j] = aug[col][j] / pv
                j = j + 1
            end while
            ' Eliminate this column from every other row.
            r = 0
            while r < n
                if r != col then
                    factor = aug[r][col]
                    j = 0
                    while j < 2 * n
                        aug[r][j] = aug[r][j] - factor * aug[col][j]
                        j = j + 1
                    end while
                end if
                r = r + 1
            end while
            col = col + 1
        end while

        ' Extract the right half — the inverse.
        inv = []
        i = 0
        while i < n
            row = []
            j = 0
            while j < n
                append(row, aug[i][n + j])
                j = j + 1
            end while
            append(inv, row)
            i = i + 1
        end while
        return inv
    end function

    ' Dot product of two equal-length vectors.
    function vec_dot(u, v)
        n = len(u)
        if len(v) != n then
            return unknown
        end if
        s = 0
        i = 0
        while i < n
            s = s + u[i] * v[i]
            i = i + 1
        end while
        return s
    end function
end library
