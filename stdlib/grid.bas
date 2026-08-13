' grid.bas — ARI-for-grids: turning a messy worksheet into clean frames.
'
' Layer 2 of docs/xlsx_design.md. Layer 0 reads the file and Layer 1 gives a
' grid of typed cells; this is the layer that copes with the fact that real
' workbooks are IRREGULAR — a title above the table, a header split over two
' rows, subtotals interleaved with data, a trailing note, several tables on one
' sheet.
'
' THE RULE THIS LIBRARY IS BUILT AROUND: automatic when safe, spec when not,
' and it tells you which. `grid.tables` guesses and reports how confident it is;
' `grid.extract` does exactly what it is told. For anything that ends up in a
' financial report the spec path is the supported one, because a totals row
' silently absorbed as data is a reporting error, not a cosmetic one.
'
' WHY THIS IS PURE gBASIC rather than C. The reading is done (xlsx.cells); what
' remains is policy — which row is a header, which row is a total — and policy
' belongs where it can be read and changed by whoever owns the report. It is
' also the same choice ARI made for print-image reports, and for the same
' reason.
'
' Output is a FRAME (stdlib/frame.bas): a record of equal-length columns, so
' results flow straight into frame/stats/chart.

library grid
    ' The frame layer, loaded INSIDE the library block -- a top-level `load`
    ' does not put the dependency in scope for this library's own functions,
    ' which fails only when a caller has not also loaded it. Same placement as
    ' stats.bas over matrix.bas.
    load frame from "frame.bas"

    ' ---------------------------------------------------------------- building

    ' A grid holds its cells as SPARSE PER-ROW ARRAYS: `rownos` ascending, and
    ' `rowdata[i]` the cells of row rownos[i] as {c, v, k} in column order.
    '
    ' The obvious design -- one record field per cell, keyed "r,c" -- is the one
    ' this replaced, and it could not run on a real sheet. A gBASIC record is a
    ' LINEAR-SCAN association list, not a hash map, so building N fields costs
    ' O(N^2): measured, 2,000 inserts take 28 ms and 16,000 take 744 ms, which
    ' puts a real 182,000-cell corpus sheet at roughly 1.6e10 comparisons. The
    ' first workbook tried timed out at 120 s.
    '
    ' Arrays are O(1) indexed, so this build is one linear pass (xlsx.cells
    ' returns row-major, so rows close as the row number changes) and a lookup
    ' is a binary search for the row plus a short scan within it -- rows are a
    ' few dozen cells wide, not thousands.
    function of(wb, sheet_name)
        rownos = []
        rowdata = []
        cur = []
        cur_row = 0 - 1
        min_c = 0
        max_c = 0
        min_r = 0
        max_r = 0
        seen = false
        for each cell in xlsx.cells(wb, sheet_name)
            rc = _split_ref(cell.ref)
            if rc.row != cur_row then
                if cur_row >= 0 then
                    append(rownos, cur_row)
                    append(rowdata, cur)
                end if
                cur = []
                cur_row = rc.row
            end if
            append(cur, { c: rc.col, v: cell.value, k: cell.kind })
            if not seen then
                min_r = rc.row
                max_r = rc.row
                min_c = rc.col
                max_c = rc.col
                seen = true
            else
                if rc.row < min_r then
                    min_r = rc.row
                end if
                if rc.row > max_r then
                    max_r = rc.row
                end if
                if rc.col < min_c then
                    min_c = rc.col
                end if
                if rc.col > max_c then
                    max_c = rc.col
                end if
            end if
        end for
        if cur_row >= 0 then
            append(rownos, cur_row)
            append(rowdata, cur)
        end if
        return { sheet: sheet_name, rownos: rownos, rowdata: rowdata,
                 first_row: min_r, last_row: max_r,
                 first_col: min_c, last_col: max_c, any: seen }
    end function

    ' "B7" -> { row: 7, col: 1 }. Columns are 0-based to match the reader.
    function _split_ref(ref)
        i = 0
        col = 0
        while i < len(ref)
            ch = mid(ref, i, 1)
            if ch >= "A" and ch <= "Z" then
                col = col * 26 + (code(ch) - code("A") + 1)
                i = i + 1
            else
                break
            end if
        end while
        return { row: number(mid(ref, i, len(ref) - i)), col: col - 1 }
    end function

    ' Index of row r in rownos, or -1. Binary search: a sheet can have tens of
    ' thousands of rows and this is called from every cell access.
    function _row_index(g, r)
        lo = 0
        hi = count(g.rownos) - 1
        while lo <= hi
            mid_i = floor((lo + hi) / 2)
            v = g.rownos[mid_i]
            if v = r then
                return mid_i
            end if
            if v < r then
                lo = mid_i + 1
            else
                hi = mid_i - 1
            end if
        end while
        return 0 - 1
    end function

    ' The cells of row r, in column order. Empty when the row is absent.
    ' Callers that walk a whole row should use THIS rather than calling `at`
    ' per column -- the row is found once instead of once per cell.
    function row_cells(g, r)
        i = _row_index(g, r)
        if i < 0 then
            return []
        end if
        return g.rowdata[i]
    end function

    function at(g, r, c)
        for each cell in row_cells(g, r)
            if cell.c = c then
                return cell.v
            end if
        end for
        return unknown
    end function

    function kind_at(g, r, c)
        for each cell in row_cells(g, r)
            if cell.c = c then
                return cell.k
            end if
        end for
        return "blank"
    end function

    function is_blank(g, r, c)
        v = at(g, r, c)
        if is_unknown(v) then
            return true
        end if
        return string(v) = ""
    end function

    function row_is_blank(g, r)
        for each cell in row_cells(g, r)
            if is_unknown(cell.v) then
                continue
            end if
            if string(cell.v) != "" then
                return false
            end if
        end for
        return true
    end function

    ' How many cells a row actually fills. Used to tell a one-cell title or note
    ' apart from a real data row.
    function row_width(g, r)
        n = 0
        for each cell in row_cells(g, r)
            if is_unknown(cell.v) then
                continue
            end if
            if string(cell.v) != "" then
                n = n + 1
            end if
        end for
        return n
    end function

    ' ------------------------------------------------------------- heuristics

    ' Contiguous runs of non-blank rows. A blank row ends a block, which is the
    ' single most reliable structural signal a spreadsheet gives.
    function blocks(g)
        out = []
        r = g.first_row
        start = 0
        inblock = false
        while r <= g.last_row
            if row_is_blank(g, r) then
                if inblock then
                    append(out, { first_row: start, last_row: r - 1 })
                    inblock = false
                end if
            else
                if not inblock then
                    start = r
                    inblock = true
                end if
            end if
            r = r + 1
        end while
        if inblock then
            append(out, { first_row: start, last_row: g.last_row })
        end if
        return out
    end function

    ' Does this row look like a header? Every filled cell is text, and there is
    ' more than one of them — a single text cell is a title, not a header.
    function _looks_header(g, r)
        n = 0
        for each cell in row_cells(g, r)
            if is_unknown(cell.v) then
                continue
            end if
            if string(cell.v) = "" then
                continue
            end if
            if cell.k != "text" then
                return false
            end if
            n = n + 1
        end for
        return n > 1
    end function

    ' A totals row: the label column matches one of the usual words. Kept as a
    ' regex so a caller can see and change exactly what is being trusted.
    function total_pattern()
        return regex("^ *(sub[- ]?total|total|grand total)\\b", "i")
    end function

    function _is_total_row(g, r, label_col)
        v = at(g, r, label_col)
        if is_unknown(v) then
            return false
        end if
        return contains(string(v), total_pattern())
    end function

    ' Best-effort table detection. Returns a candidate per block, each with a
    ' CONFIDENCE and the reasons behind it — never a silent commitment.
    function tables(g)
        out = []
        for each b in blocks(g)
            ' A block of one row that fills one cell is a title or a note, not
            ' a table. Reported, not silently dropped.
            width = row_width(g, b.first_row)
            if b.first_row = b.last_row and width <= 1 then
                append(out, { first_row: b.first_row, last_row: b.last_row,
                              header_row: unknown, confidence: "none",
                              notes: ["single filled cell: a title or note, not a table"],
                              frame: unknown })
                continue
            end if

            notes = []
            hdr = unknown
            r = b.first_row
            while r <= b.last_row
                if _looks_header(g, r) then
                    hdr = r
                    break
                end if
                r = r + 1
            end while

            conf = "high"
            if is_unknown(hdr) then
                conf = "none"
                append(notes, "no all-text row found: cannot name the columns")
                append(out, { first_row: b.first_row, last_row: b.last_row,
                              header_row: unknown, confidence: conf,
                              notes: notes, frame: unknown })
                continue
            end if
            if hdr > b.first_row then
                append(notes, "rows above the header were skipped (title or stub)")
                conf = "medium"
            end if
            ' A second all-text row directly BELOW is the giveaway for a
            ' two-row header: the row found first is then the PARENT level, and
            ' naming the columns from it alone loses every child name. Checked
            ' below rather than above because detection scans downward and
            ' stops at the first candidate, which is the parent.
            if _looks_header(g, hdr + 1) then
                append(notes, "the row below also looks like a header: two-row header? pass header_rows: 2")
                conf = "low"
            end if
            ' Totals inside the block are the dangerous case, because adding
            ' them to the data double-counts and still looks plausible.
            tot = 0
            rr = hdr + 1
            while rr <= b.last_row
                if _is_total_row(g, rr, g.first_col) then
                    tot = tot + 1
                end if
                rr = rr + 1
            end while
            if tot > 0 then
                append(notes, "contains " + tot + " row(s) that look like totals: pass drop_totals: true")
                conf = "low"
            end if

            f = _frame_from(g, hdr, 1, hdr + 1, b.last_row, false, [])
            append(out, { first_row: b.first_row, last_row: b.last_row,
                          header_row: hdr, confidence: conf, notes: notes,
                          frame: f })
        end for
        return out
    end function

    ' ------------------------------------------------------------------ spec

    ' Extract exactly what the spec says. Fields, all optional:
    '
    '   starts        text or regex the label column must match to begin
    '   ends          text or regex that ends the region (exclusive)
    '   header_row    absolute row of the header
    '   header_rows   how many rows the header spans (default 1); a parent
    '                 level is carried across blank cells and joined with " "
    '   first_row     explicit data start (overrides header detection)
    '   last_row      explicit data end
    '   break_on_blank   stop at the first blank row (default true)
    '   drop_totals   drop rows whose label matches total_pattern()
    '   drop_matching a regex; drop rows whose label column matches it
    '   label_col     which column holds the row label (default first)
    '   columns       explicit list of names, overriding the header
    function extract(g, spec)
        label_col = g.first_col
        if has(spec, "label_col") then
            label_col = spec.label_col
        end if

        ' Where the region starts.
        first = g.first_row
        if has(spec, "starts") then
            r = g.first_row
            found = false
            while r <= g.last_row
                v = at(g, r, label_col)
                if not is_unknown(v) then
                    if contains(string(v), spec.starts) then
                        first = r
                        found = true
                        break
                    end if
                end if
                r = r + 1
            end while
            if not found then
                return { ok: false, message: "no row matched `starts`", frame: unknown }
            end if
        end if

        hdr_rows = 1
        if has(spec, "header_rows") then
            hdr_rows = spec.header_rows
        end if
        hdr = first
        if has(spec, "header_row") then
            hdr = spec.header_row
            first = hdr
        end if

        data_first = hdr + hdr_rows
        if has(spec, "first_row") then
            data_first = spec.first_row
        end if

        ' Where it ends: an explicit row, an `ends` match, or the first blank.
        last = g.last_row
        if has(spec, "last_row") then
            last = spec.last_row
        else
            brk = true
            if has(spec, "break_on_blank") then
                brk = spec.break_on_blank
            end if
            r = data_first
            while r <= g.last_row
                stop_here = false
                if brk and row_is_blank(g, r) then
                    stop_here = true
                end if
                if has(spec, "ends") then
                    v = at(g, r, label_col)
                    if not is_unknown(v) then
                        if contains(string(v), spec.ends) then
                            stop_here = true
                        end if
                    end if
                end if
                if stop_here then
                    last = r - 1
                    break
                end if
                last = r
                r = r + 1
            end while
        end if

        drop_tot = false
        if has(spec, "drop_totals") then
            drop_tot = spec.drop_totals
        end if
        drops = []
        if has(spec, "drop_matching") then
            append(drops, spec.drop_matching)
        end if
        if drop_tot then
            append(drops, total_pattern())
        end if

        f = _frame_from(g, hdr, hdr_rows, data_first, last, true, drops)
        if has(spec, "columns") then
            f = _rename_positional(f, spec.columns)
        end if
        return { ok: true, message: "", header_row: hdr,
                 first_row: data_first, last_row: last, frame: f }
    end function

    ' ------------------------------------------------------------- the frame

    ' Column names from `hdr_rows` header rows. A parent level is CARRIED
    ' ACROSS BLANK CELLS: Excel writes a merged "Q1" spanning two columns as
    ' text in the first and nothing in the second, so without carrying it the
    ' second column loses its qualifier and two columns end up both named
    ' "Units".
    function _names(g, hdr, hdr_rows, use_drops)
        names = []
        cols = []
        c = g.first_col
        while c <= g.last_col
            parts = []
            carried = ""
            k = 0
            while k < hdr_rows - 1
                rr = hdr + k
                cc = g.first_col
                carried = ""
                while cc <= c
                    v = at(g, rr, cc)
                    if not is_unknown(v) then
                        if string(v) != "" then
                            carried = string(v)
                        end if
                    end if
                    cc = cc + 1
                end while
                if carried != "" then
                    append(parts, carried)
                end if
                k = k + 1
            end while
            leaf = at(g, hdr + hdr_rows - 1, c)
            if is_unknown(leaf) or string(leaf) = "" then
                ' A column with no leaf name is a spacer: skipped entirely, not
                ' emitted as an empty-named column.
                c = c + 1
                continue
            end if
            append(parts, string(leaf))
            append(names, join(parts, " "))
            append(cols, c)
            c = c + 1
        end while
        return { names: names, cols: cols }
    end function

    function _frame_from(g, hdr, hdr_rows, first, last, use_drops, drops)
        h = _names(g, hdr, hdr_rows, use_drops)
        rows = []
        r = first
        while r <= last
            skip = false
            if row_is_blank(g, r) then
                skip = true
            end if
            if not skip then
                if count(drops) > 0 then
                    lv = at(g, r, g.first_col)
                    if not is_unknown(lv) then
                        for each d in drops
                            if contains(string(lv), d) then
                                skip = true
                            end if
                        end for
                    end if
                end if
            end if
            if not skip then
                rec = { }
                i = 0
                while i < count(h.names)
                    rec[h.names[i]] = at(g, r, h.cols[i])
                    i = i + 1
                end while
                append(rows, rec)
            end if
            r = r + 1
        end while
        return frame.from_rows(rows)
    end function

    function _rename_positional(f, names)
        old = frame.columns(f)
        mapping = { }
        i = 0
        while i < count(old) and i < count(names)
            mapping[old[i]] = names[i]
            i = i + 1
        end while
        return frame.rename(f, mapping)
    end function

end library
