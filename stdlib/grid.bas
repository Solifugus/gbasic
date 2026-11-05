' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

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

    ' A grid holds its cells as PARALLEL PER-ROW ARRAYS: `rownos` ascending,
    ' and for row i the columns, values and kinds in `rcols[i]`, `rvals[i]`,
    ' `rkinds[i]`.
    '
    ' TWO REPRESENTATIONS WERE MEASURED AND DISCARDED before this one, both on
    ' real workbooks rather than on the small fixture:
    '
    '   * one RECORD FIELD PER CELL keyed "r,c". A gBASIC record is a
    '     linear-scan association list, not a hash map, so building N fields is
    '     O(N^2) -- 2,000 inserts 28 ms, 16,000 inserts 744 ms. A 182,000-cell
    '     sheet is ~1.6e10 comparisons; the first corpus file tried timed out
    '     at 120 s.
    '   * one RECORD PER CELL inside per-row arrays. Correct and linear, but a
    '     three-field record per cell cost about 300x the file size in RAM --
    '     194 MB peak for a 648 KB workbook -- which made 14-way parallel
    '     scanning fail on memory rather than on any defect.
    '
    ' Parallel arrays keep one record per ROW instead of one per CELL, which is
    ' roughly an order of magnitude fewer allocations on a real sheet, and
    ' arrays are contiguous where records are not.
    function of(wb, sheet_name)
        ' The whole build is one C call now. Doing it in gBASIC meant reading
        ' xlsx.cells, which materialises a five-field RECORD per cell -- 192 MB
        ' for the 78,124 cells of one 648 KB corpus workbook, about 2.5 KB a
        ' cell. xlsx.grid returns the same information column-oriented, one
        ' allocation per row instead of five per cell.
        return xlsx.grid(wb, sheet_name)
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
    ' thousands of rows and this is reached from every cell access.
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

    ' Position of column c within row-index i, or -1.
    function _col_index(g, i, c)
        cols = g.rcols[i]
        j = 0
        while j < count(cols)
            if cols[j] = c then
                return j
            end if
            j = j + 1
        end while
        return 0 - 1
    end function

    function at(g, r, c)
        i = _row_index(g, r)
        if i < 0 then
            return unknown
        end if
        j = _col_index(g, i, c)
        if j < 0 then
            return unknown
        end if
        return g.rvals[i][j]
    end function

    function kind_at(g, r, c)
        i = _row_index(g, r)
        if i < 0 then
            return "blank"
        end if
        j = _col_index(g, i, c)
        if j < 0 then
            return "blank"
        end if
        return g.rkinds[i][j]
    end function

    function is_blank(g, r, c)
        v = at(g, r, c)
        if is_unknown(v) then
            return true
        end if
        return string(v) = ""
    end function

    function row_is_blank(g, r)
        i = _row_index(g, r)
        if i < 0 then
            return true
        end if
        for each v in g.rvals[i]
            if is_unknown(v) then
                continue
            end if
            if string(v) != "" then
                return false
            end if
        end for
        return true
    end function

    ' How many cells a row actually fills. Used to tell a one-cell title or note
    ' apart from a real data row.
    function row_width(g, r)
        i = _row_index(g, r)
        if i < 0 then
            return 0
        end if
        n = 0
        for each v in g.rvals[i]
            if is_unknown(v) then
                continue
            end if
            if string(v) != "" then
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
        i = _row_index(g, r)
        if i < 0 then
            return false
        end if
        n = 0
        vals = g.rvals[i]
        kinds = g.rkinds[i]
        j = 0
        while j < count(vals)
            v = vals[j]
            skip = is_unknown(v)
            if not skip then
                if string(v) = "" then
                    skip = true
                end if
            end if
            if not skip then
                if kinds[j] != "text" then
                    return false
                end if
                n = n + 1
            end if
            j = j + 1
        end while
        return n > 1
    end function

    ' A totals row: the label column matches one of the usual words. Kept as a
    ' regex so a caller can see and change exactly what is being trusted.
    ' The label may BEGIN with the word ("Total", "Subtotal - East") or END with
    ' it ("Garza subtotal", "Aisle A total"): a pattern for the first alone
    ' let "<name> subtotal" rows through drop_totals as data, 21 rows for 18
    ' visits (gap G22 from the gBASIC books).
    function total_pattern()
        return regex("^ *(sub[- ]?total|total|grand total)\\b|\\b(sub[- ]?total|total)\\s*:?\\s*$", "i")
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

            built = _frame_from(g, hdr, 1, hdr + 1, b.last_row, false, [], g.first_col, true)
            for each n in built.notes
                append(notes, n)
            end for
            append(out, { first_row: b.first_row, last_row: b.last_row,
                          header_row: hdr, confidence: conf, notes: notes,
                          frame: built.frame })
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
    '   carry_parents a parent heading over blank cells to its right: where
    '                 the sheet declares merges only across its merge range,
    '                 and with none, as far as the next heading (default true)
    '
    ' The result says where every frame row came from (gap G20 from the gBASIC
    ' books): `rows`, the sheet row of each frame row in order; `dropped`,
    ' every row inside the region left out, as { row, label, why } with why
    ' "blank", "total" or "drop_matching"; and `header`, each column's
    ' { name, col }. A heading written twice is two columns, the second named
    ' "<name> (2)" and reported in `notes`; it was one column, the second's
    ' values winning, without a word.
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

        carry = true
        if has(spec, "carry_parents") then
            carry = spec.carry_parents
        end if
        built = _frame_from(g, hdr, hdr_rows, data_first, last, true, drops, label_col, carry)
        f = built.frame
        if has(spec, "columns") then
            f = _rename_positional(f, spec.columns)
        end if
        return { ok: true, message: "", header_row: hdr,
                 first_row: data_first, last_row: last, frame: f,
                 rows: built.rows, dropped: built.dropped, header: built.header, notes: built.notes }
    end function

    ' ------------------------------------------------------------- the frame

    ' The merge range holding (r, c), or unknown.
    function _merge_at(g, r, c)
        if not has(g, "merges") then
            return unknown
        end if
        for each m in g.merges
            if r >= m.first_row and r <= m.last_row and c >= m.first_col and c <= m.last_col then
                return m
            end if
        end for
        return unknown
    end function

    ' Column names from `hdr_rows` header rows. A parent heading spans what
    ' its MERGE spans: Excel writes "Visit" merged over A-C as text in A and
    ' nothing in B and C, so B and C take it from the merge. With no merge the
    ' sheet says nothing, and a sheet that declares merges is believed: South's
    ' "Parts" over F alone did not name G's "Warranty" (gap G19 from the
    ' gBASIC books; carrying across every blank cell made it "Parts
    ' Warranty"). A sheet that declares NO merges (written by a program, or
    ' saved without them) keeps the old reading, carried to the next heading,
    ' unless `carry` is false.
    function _names(g, hdr, hdr_rows, use_drops, carry)
        names = []
        cols = []
        uses_merges = false
        if has(g, "merges") then
            uses_merges = count(g.merges) > 0
        end if
        c = g.first_col
        while c <= g.last_col
            parts = []
            k = 0
            while k < hdr_rows - 1
                rr = hdr + k
                parent = ""
                v = at(g, rr, c)
                if not is_unknown(v) then
                    parent = string(v)
                end if
                if parent = "" and uses_merges then
                    m = _merge_at(g, rr, c)
                    if not is_unknown(m) then
                        tl = at(g, m.first_row, m.first_col)
                        if not is_unknown(tl) then
                            parent = string(tl)
                        end if
                    end if
                else if parent = "" and carry then
                    cc = c - 1
                    while cc >= g.first_col and parent = ""
                        v = at(g, rr, cc)
                        if not is_unknown(v) then
                            parent = string(v)
                        end if
                        cc = cc - 1
                    end while
                end if
                if parent != "" then
                    append(parts, parent)
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

    ' The frame, and where each of its rows came from: { frame, rows, dropped,
    ' header, notes }. The drop check reads `label_col`, where `starts` and
    ' `ends` look; it read the first column whatever label_col said.
    function _frame_from(g, hdr, hdr_rows, first, last, use_drops, drops, label_col, carry)
        h = _names(g, hdr, hdr_rows, use_drops, carry)
        notes = []
        header = []
        seen = {}
        names = []
        i = 0
        while i < count(h.names)
            n = h.names[i]
            if has(seen, n) then
                seen[n] = seen[n] + 1
                n2 = n + " (" + string(seen[n]) + ")"
                append(notes, "the heading \"" + n + "\" is written twice; its column " + string(h.cols[i]) + " is \"" + n2 + "\"")
                n = n2
            else
                seen[n] = 1
            end if
            append(names, n)
            append(header, { name: n, col: h.cols[i] })
            i = i + 1
        end while
        rows = []
        kept = []
        dropped = []
        r = first
        while r <= last
            why = ""
            lv = at(g, r, label_col)
            if row_is_blank(g, r) then
                why = "blank"
            end if
            if why = "" and count(drops) > 0 and not is_unknown(lv) then
                for each d in drops
                    if why = "" and contains(string(lv), d) then
                        if string(d) = string(total_pattern()) then
                            why = "total"
                        else
                            why = "drop_matching"
                        end if
                    end if
                end for
            end if
            if why != "" then
                label = unknown
                if not is_unknown(lv) then
                    label = string(lv)
                end if
                append(dropped, { row: r, label: label, why: why })
            else
                rec = { }
                i = 0
                while i < count(names)
                    rec[names[i]] = at(g, r, h.cols[i])
                    i = i + 1
                end while
                append(rows, rec)
                append(kept, r)
            end if
            r = r + 1
        end while
        return { frame: frame.from_rows(rows), rows: kept, dropped: dropped, header: header, notes: notes }
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
