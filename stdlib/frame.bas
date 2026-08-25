' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' frame.bas — the structural data-frame layer (statistics_design.md §4).
'
' A frame is a plain record whose fields are equal-length lists (column-major:
' a dict of columns). No new value kind — a frame is inspectable plain data, so
' a column is just a list and the whole univariate-stats surface is ordinary
' list functions: mean(df.age) already works. This module adds the structural
' layer — import, clean, reshape, group, join, inspect — each transform
' returning a NEW frame (PBI copy-on-write makes untouched columns shared).
'
' Missing data is `unknown` (gBASIC's native NA): an empty cell on import
' becomes unknown, and list stats skip it by convention.
library frame
    ' --- Representation helpers ---

    function _ncols(df)
        return len(keys(df))
    end function

    function _nrows(df)
        names = keys(df)
        if len(names) = 0 then
            return 0
        end if
        return len(df[names[0]])
    end function

    ' Build the row-record for row i (the list-of-records view of one row).
    function _row_at(df, i)
        names = keys(df)
        row = {}
        c = 0
        while c < len(names)
            row[names[c]] = df[names[c]][i]
            c = c + 1
        end while
        return row
    end function

    ' A new frame with the same columns as df but empty.
    function _empty_like(df)
        names = keys(df)
        out = {}
        c = 0
        while c < len(names)
            out[names[c]] = []
            c = c + 1
        end while
        return out
    end function

    ' --- Inspect ---

    function columns(df)
        return keys(df)
    end function

    function shape(df)
        return [_nrows(df), _ncols(df)]
    end function

    ' Inferred type per column (from the first non-missing cell; "unknown" if a
    ' column is empty or all-missing).
    function dtypes(df)
        names = keys(df)
        out = {}
        c = 0
        while c < len(names)
            col = df[names[c]]
            t = "unknown"
            i = 0
            while i < len(col)
                if not is_unknown(col[i]) then
                    t = type(col[i])
                    break
                end if
                i = i + 1
            end while
            out[names[c]] = t
            c = c + 1
        end while
        return out
    end function

    ' The first n rows as a new frame.
    function head(df, n)
        names = keys(df)
        out = {}
        c = 0
        while c < len(names)
            col = df[names[c]]
            slice = []
            i = 0
            while i < n
                if i < len(col) then
                    append(slice, col[i])
                end if
                i = i + 1
            end while
            out[names[c]] = slice
            c = c + 1
        end while
        return out
    end function

    ' --- Conversions (exact inverses for a well-formed frame) ---

    function to_rows(df)
        n = _nrows(df)
        rows = []
        i = 0
        while i < n
            append(rows, _row_at(df, i))
            i = i + 1
        end while
        return rows
    end function

    function from_rows(rows)
        if len(rows) = 0 then
            return {}
        end if
        names = keys(rows[0])
        df = {}
        c = 0
        while c < len(names)
            df[names[c]] = []
            c = c + 1
        end while
        i = 0
        while i < len(rows)
            c = 0
            while c < len(names)
                append(df[names[c]], rows[i][names[c]])
                c = c + 1
            end while
            i = i + 1
        end while
        return df
    end function

    ' --- Select / reshape (each returns a new frame) ---

    ' Keep the named columns, in the order given.
    function select(df, cols)
        out = {}
        c = 0
        while c < len(cols)
            if has(df, cols[c]) then
                out[cols[c]] = df[cols[c]]
            end if
            c = c + 1
        end while
        return out
    end function

    ' Drop the named columns.
    function drop(df, cols)
        names = keys(df)
        out = {}
        c = 0
        while c < len(names)
            if not contains(cols, names[c]) then
                out[names[c]] = df[names[c]]
            end if
            c = c + 1
        end while
        return out
    end function

    ' Rename columns via a record mapping {old: new}; unlisted columns keep
    ' their names and order.
    function rename(df, mapping)
        names = keys(df)
        out = {}
        c = 0
        while c < len(names)
            nm = names[c]
            if has(mapping, nm) then
                out[mapping[nm]] = df[nm]
            else
                out[nm] = df[nm]
            end if
            c = c + 1
        end while
        return out
    end function

    ' Keep rows for which pred(row) is true. pred is a user function value
    ' taking a row-record and returning a boolean.
    function filter(df, pred)
        names = keys(df)
        n = _nrows(df)
        out = _empty_like(df)
        i = 0
        while i < n
            if pred(_row_at(df, i)) then
                c = 0
                while c < len(names)
                    append(out[names[c]], df[names[c]][i])
                    c = c + 1
                end while
            end if
            i = i + 1
        end while
        return out
    end function

    ' Add (or replace) a column computed per row by fn(row).
    function with_column(df, name, fn)
        n = _nrows(df)
        col = []
        i = 0
        while i < n
            append(col, fn(_row_at(df, i)))
            i = i + 1
        end while
        out = df
        out[name] = col
        return out
    end function

    ' Retype one column to "number" or "string". Missing cells stay unknown;
    ' values that cannot become numbers become unknown.
    function coerce(df, colname, typename)
        src = df[colname]
        newcol = []
        i = 0
        while i < len(src)
            v = src[i]
            if is_unknown(v) then
                append(newcol, unknown)
            else
                if typename = "number" then
                    if is_number(v) then
                        append(newcol, v)
                    else
                        s = trim(string(v))
                        if _looks_numeric(s) then
                            append(newcol, number(s))
                        else
                            append(newcol, unknown)
                        end if
                    end if
                else
                    append(newcol, string(v))
                end if
            end if
            i = i + 1
        end while
        out = df
        out[colname] = newcol
        return out
    end function

    ' Replace missing cells in one column with a fixed value.
    function fill_missing(df, colname, value)
        src = df[colname]
        newcol = []
        i = 0
        while i < len(src)
            if is_unknown(src[i]) then
                append(newcol, value)
            else
                append(newcol, src[i])
            end if
            i = i + 1
        end while
        out = df
        out[colname] = newcol
        return out
    end function

    ' Drop every row that has any missing cell.
    function drop_missing(df)
        names = keys(df)
        n = _nrows(df)
        out = _empty_like(df)
        i = 0
        while i < n
            ok = true
            c = 0
            while c < len(names)
                if is_unknown(df[names[c]][i]) then
                    ok = false
                    break
                end if
                c = c + 1
            end while
            if ok then
                c = 0
                while c < len(names)
                    append(out[names[c]], df[names[c]][i])
                    c = c + 1
                end while
            end if
            i = i + 1
        end while
        return out
    end function

    ' Drop duplicate rows, keeping first occurrence (compared by serialization).
    function dedupe(df)
        names = keys(df)
        n = _nrows(df)
        out = _empty_like(df)
        seen = []
        i = 0
        while i < n
            sig = serialize(_row_at(df, i))
            if not contains(seen, sig) then
                append(seen, sig)
                c = 0
                while c < len(names)
                    append(out[names[c]], df[names[c]][i])
                    c = c + 1
                end while
            end if
            i = i + 1
        end while
        return out
    end function

    ' Order comparison that sends `unknown` to the end; otherwise the native
    ' `<` on numbers or strings.
    function _less(a, b)
        if is_unknown(a) then
            return false
        end if
        if is_unknown(b) then
            return true
        end if
        return a < b
    end function

    ' Sort rows ascending by one column (stable insertion sort; row counts in
    ' analysis frames are modest, and this keeps ties in input order).
    function sort_by(df, colname)
        rows = to_rows(df)
        n = len(rows)
        i = 1
        while i < n
            cur = rows[i]
            j = i - 1
            while j >= 0
                if _less(cur[colname], rows[j][colname]) then
                    rows[j + 1] = rows[j]
                    j = j - 1
                else
                    break
                end if
            end while
            rows[j + 1] = cur
            i = i + 1
        end while
        ' Rebuild preserving the original column order (from_rows uses row 0's
        ' key order, which is the same since every row shares the schema).
        if n = 0 then
            return _empty_like(df)
        end if
        return from_rows(rows)
    end function

    ' --- Numeric-string helpers (used by read_csv inference and coerce) ---

    ' True if s is a plain decimal number (optional sign, digits, at most one
    ' dot). Scientific notation is intentionally excluded so inference never
    ' hands number() a string it would reject.
    function _looks_numeric(s)
        t = trim(s)
        n = len(t)
        if n = 0 then
            return false
        end if
        i = 0
        c0 = code(mid(t, 0, 1))
        if c0 = 43 then
            i = 1
        end if
        if c0 = 45 then
            i = 1
        end if
        digits = 0
        dots = 0
        while i < n
            c = code(mid(t, i, 1))
            if c >= 48 then
                if c <= 57 then
                    digits = digits + 1
                else
                    if c = 46 then
                        dots = dots + 1
                        if dots > 1 then
                            return false
                        end if
                    else
                        return false
                    end if
                end if
            else
                if c = 46 then
                    dots = dots + 1
                    if dots > 1 then
                        return false
                    end if
                else
                    return false
                end if
            end if
            i = i + 1
        end while
        if digits = 0 then
            return false
        end if
        return true
    end function

    ' True if s is an integer that would lose precision as a double: a leading
    ' zero (zip/account codes) or more than 15 digits. Such columns are kept as
    ' strings on import (statistics_design.md §4.1).
    function _is_id_like(s)
        t = trim(s)
        n = len(t)
        if n = 0 then
            return false
        end if
        start = 0
        c0 = code(mid(t, 0, 1))
        if c0 = 43 then
            start = 1
        end if
        if c0 = 45 then
            start = 1
        end if
        i = start
        while i < n
            c = code(mid(t, i, 1))
            if c < 48 then
                return false
            end if
            if c > 57 then
                return false
            end if
            i = i + 1
        end while
        digits = n - start
        if digits = 0 then
            return false
        end if
        if digits > 1 then
            if code(mid(t, start, 1)) = 48 then
                return true
            end if
        end if
        if digits > 15 then
            return true
        end if
        return false
    end function

    ' Value equality that also works for `unknown` and mixed kinds (compares
    ' canonical serializations), used for grouping and join keys.
    function _same(a, b)
        return serialize(a) = serialize(b)
    end function

    ' --- Group / summarize (split-apply-combine) ---
    '
    ' Split df into per-group sub-frames by the `by` column, apply each
    ' aggregator to its sub-frame, and combine into a new frame. `aggs` is a
    ' record {out_name: fn(group_frame) -> value}; each fn is a user function
    ' value (e.g. function avg_age(g) return mean(g.age) end function). The
    ' result has the `by` column (one row per distinct value, in first-seen
    ' order) plus one column per aggregator.
    function summarize(df, by, aggs)
        names = keys(df)
        n = _nrows(df)

        ' Distinct group keys in first-seen order, tracked by signature.
        sigs = []
        groupkeys = []
        i = 0
        while i < n
            s = serialize(df[by][i])
            if not contains(sigs, s) then
                append(sigs, s)
                append(groupkeys, df[by][i])
            end if
            i = i + 1
        end while

        aggnames = keys(aggs)
        out = {}
        out[by] = []
        a = 0
        while a < len(aggnames)
            out[aggnames[a]] = []
            a = a + 1
        end while

        g = 0
        while g < len(groupkeys)
            gsig = sigs[g]
            sub = _empty_like(df)
            i = 0
            while i < n
                if serialize(df[by][i]) = gsig then
                    c = 0
                    while c < len(names)
                        append(sub[names[c]], df[names[c]][i])
                        c = c + 1
                    end while
                end if
                i = i + 1
            end while
            append(out[by], groupkeys[g])
            a = 0
            while a < len(aggnames)
                fn = aggs[aggnames[a]]
                append(out[aggnames[a]], fn(sub))
                a = a + 1
            end while
            g = g + 1
        end while
        return out
    end function

    ' --- Join ---
    '
    ' Inner join of two frames on a shared key column. Output is all of left's
    ' columns followed by right's non-key columns; a right column whose name
    ' collides with a left column is suffixed "_right". Every matching pair of
    ' rows produces one output row.
    function join(left, right, key)
        lnames = keys(left)
        rnames = keys(right)

        out = {}
        c = 0
        while c < len(lnames)
            out[lnames[c]] = []
            c = c + 1
        end while

        ' Output name for each right column ("" marks the key, which is skipped).
        routnames = []
        c = 0
        while c < len(rnames)
            rn = rnames[c]
            if rn = key then
                append(routnames, "")
            else
                oname = rn
                if has(out, rn) then
                    oname = rn + "_right"
                end if
                out[oname] = []
                append(routnames, oname)
            end if
            c = c + 1
        end while

        ln = _nrows(left)
        rcount = _nrows(right)
        i = 0
        while i < ln
            lk = left[key][i]
            j = 0
            while j < rcount
                if _same(lk, right[key][j]) then
                    c = 0
                    while c < len(lnames)
                        append(out[lnames[c]], left[lnames[c]][i])
                        c = c + 1
                    end while
                    c = 0
                    while c < len(rnames)
                        if rnames[c] != key then
                            append(out[routnames[c]], right[rnames[c]][j])
                        end if
                        c = c + 1
                    end while
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        return out
    end function

    ' --- CSV import / export ---
    '
    ' read_csv infers each column's type: a column becomes numeric only when
    ' every non-empty cell is a plain decimal AND none is ID-like (leading zero
    ' or > 15 digits — kept as strings to avoid silent precision loss, §4.1).
    ' Empty cells become `unknown`. Simple comma splitting only: quoted fields
    ' and embedded commas/newlines are not yet handled.
    function read_csv(path)
        source{file}= path
        lines = read_lines(source)
        if len(lines) = 0 then
            return {}
        end if

        ' Header -> trimmed column names, in order.
        names = []
        h = split(lines[0], ",")
        c = 0
        while c < len(h)
            append(names, trim(h[c]))
            c = c + 1
        end while
        ncol = len(names)

        ' Gather raw string cells per column (skipping blank lines).
        raw = []
        c = 0
        while c < ncol
            append(raw, [])
            c = c + 1
        end while
        r = 1
        while r < len(lines)
            if len(trim(lines[r])) > 0 then
                cells = split(lines[r], ",")
                c = 0
                while c < ncol
                    cell = ""
                    if c < len(cells) then
                        cell = cells[c]
                    end if
                    append(raw[c], cell)
                    c = c + 1
                end while
            end if
            r = r + 1
        end while

        ' Infer per column, then build the typed column.
        df = {}
        c = 0
        while c < ncol
            col = raw[c]
            numeric = true
            anyval = false
            i = 0
            while i < len(col)
                cell = trim(col[i])
                if len(cell) > 0 then
                    anyval = true
                    if not _looks_numeric(cell) then
                        numeric = false
                    end if
                    if _is_id_like(cell) then
                        numeric = false
                    end if
                end if
                i = i + 1
            end while
            if not anyval then
                numeric = false
            end if

            out = []
            i = 0
            while i < len(col)
                cell = trim(col[i])
                if len(cell) = 0 then
                    append(out, unknown)
                else
                    if numeric then
                        append(out, number(cell))
                    else
                        append(out, col[i])
                    end if
                end if
                i = i + 1
            end while
            df[names[c]] = out
            c = c + 1
        end while
        return df
    end function

    ' Write a frame to CSV. Missing cells are written as empty fields.
    function write_csv(df, path)
        names = keys(df)
        n = _nrows(df)
        text = ""
        c = 0
        while c < len(names)
            if c > 0 then
                text = text + ","
            end if
            text = text + names[c]
            c = c + 1
        end while
        text = text + chr(10)
        i = 0
        while i < n
            c = 0
            while c < len(names)
                if c > 0 then
                    text = text + ","
                end if
                cell = df[names[c]][i]
                if not is_unknown(cell) then
                    text = text + string(cell)
                end if
                c = c + 1
            end while
            text = text + chr(10)
            i = i + 1
        end while
        sink{file}= path
        write(sink, text)
        return path
    end function

    ' --- Pretty-print ---

    function _cell_str(v)
        if is_unknown(v) then
            return ""
        end if
        return string(v)
    end function

    function _pad(s, w)
        out = s
        while len(out) < w
            out = out + " "
        end while
        return out
    end function

    ' Print df as an aligned text table (header, dashed rule, rows). Returns the
    ' frame so it can be chained.
    function show(df)
        names = keys(df)
        n = _nrows(df)

        ' Column widths: max of header and every cell.
        widths = []
        c = 0
        while c < len(names)
            w = len(names[c])
            i = 0
            while i < n
                cw = len(_cell_str(df[names[c]][i]))
                if cw > w then
                    w = cw
                end if
                i = i + 1
            end while
            append(widths, w)
            c = c + 1
        end while

        ' Header.
        line = ""
        c = 0
        while c < len(names)
            if c > 0 then
                line = line + "  "
            end if
            line = line + _pad(names[c], widths[c])
            c = c + 1
        end while
        print(line)

        ' Rule.
        line = ""
        c = 0
        while c < len(names)
            if c > 0 then
                line = line + "  "
            end if
            d = 0
            while d < widths[c]
                line = line + "-"
                d = d + 1
            end while
            c = c + 1
        end while
        print(line)

        ' Rows.
        i = 0
        while i < n
            line = ""
            c = 0
            while c < len(names)
                if c > 0 then
                    line = line + "  "
                end if
                line = line + _pad(_cell_str(df[names[c]][i]), widths[c])
                c = c + 1
            end while
            print(line)
            i = i + 1
        end while
        return df
    end function
end library
