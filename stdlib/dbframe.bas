' dbframe.bas — L4, the easy half: a frame becomes a database table.
'
' docs/xlsx_design.md §7 sequences this before the formula compiler and calls it
' "valuable on its own", which it is: once L2 has extracted frames and L3 has
' consolidated them, the remaining step for any real workflow is getting the
' result somewhere it can be queried and joined.
'
' Two things this refuses to do, both for the same reason as the rest of the
' module — a wrong number that looks right is worse than a failure:
'
'   * It will not guess a column type from ONE value. The type is decided from
'     every value in the column, and a column that mixes kinds becomes TEXT
'     rather than silently dropping the values that do not fit.
'   * It will not build SQL by pasting values into a string. Every value is
'     BOUND, so a loan id of `O'Brien` or `'); drop table loans;--` is data.
'     Identifiers (table and column names) cannot be bound by SQL, so those are
'     quoted and validated instead, and a name that cannot be made safe is
'     refused by name.
'
' Types are inferred to SQLite's affinities: INTEGER, REAL, TEXT. `unknown`
' becomes NULL, which is what it means.

library dbframe
    load frame from "frame.bas"
    ' Loaded here so a caller need not know this library's dependencies --
    ' declaring it in the library block is what puts it in scope for these
    ' functions (see the 2026-08-11 DOGFOOD entry).
    load sqlite

    ' ------------------------------------------------------------ identifiers

    ' A SQL identifier we are willing to emit. Letters, digits and underscore,
    ' not starting with a digit. Anything else is REFUSED rather than escaped:
    ' the caller controls these names (they come from a consolidation spec, not
    ' from data), so a rejection is a spec bug to fix, not a runtime surprise
    ' to paper over.
    function safe_name(s)
        t = string(s)
        if t = "" then
            return false
        end if
        first = mid(t, 0, 1)
        if not contains(first, regex("^[A-Za-z_]$")) then
            return false
        end if
        return contains(t, regex("^[A-Za-z_][A-Za-z0-9_]*$"))
    end function

    ' Turn a spreadsheet column heading into a usable column name: lowercase,
    ' runs of anything else collapsed to one underscore. "Q1 Units" ->
    ' "q1_units", "Rate (%)" -> "rate". Returned so the caller can see and
    ' override it — a silent rename is how a report ends up joined on the wrong
    ' column.
    function to_identifier(s)
        t = lower(trim(string(s)))
        t = replace(t, regex("[^a-z0-9]+"), "_")
        t = replace(t, regex("^_+"), "")
        t = replace(t, regex("_+$"), "")
        if t = "" then
            return ""
        end if
        if contains(mid(t, 0, 1), regex("^[0-9]$")) then
            t = "c_" + t
        end if
        return t
    end function

    ' ------------------------------------------------------------------ types

    ' The SQL type for one column, decided from EVERY value in it.
    '
    ' A column of whole numbers is INTEGER, a column with any fractional number
    ' is REAL, and anything else — including a column that mixes numbers with
    ' text — is TEXT. Mixing is the case worth stating: taking the first value's
    ' type and coercing the rest would turn "n/a" into 0 or drop it, and a zero
    ' that should have been a gap changes a total.
    function column_type(values)
        seen_num = false
        seen_frac = false
        seen_other = false
        seen_any = false
        for each v in values
            if is_unknown(v) then
                continue
            end if
            seen_any = true
            if is_number(v) then
                seen_num = true
                if v != floor(v) then
                    seen_frac = true
                end if
            else
                if is_boolean(v) then
                    seen_num = true
                else
                    seen_other = true
                end if
            end if
        end for
        if not seen_any then
            return "TEXT"
        end if
        if seen_other then
            return "TEXT"
        end if
        if seen_frac then
            return "REAL"
        end if
        if seen_num then
            return "INTEGER"
        end if
        return "TEXT"
    end function

    ' The inferred schema: a list of { name, column, type }, where `name` is the
    ' frame's column and `column` the identifier that will be created.
    function schema(df)
        out = []
        for each name in frame.columns(df)
            append(out, { name: name,
                          column: to_identifier(name),
                          type: column_type(df[name]) })
        end for
        return out
    end function

    ' ------------------------------------------------------------------- DDL

    ' CREATE TABLE text for a frame. Returned rather than executed so it can be
    ' inspected, diffed and version-controlled — the same property §6 wanted for
    ' the mapping spec.
    function create_sql(table, df)
        return _create_sql(table, df, false)
    end function

    ' The append path must not fail merely because the table is already there,
    ' so it asks for `if not exists`. A genuine schema MISMATCH still fails --
    ' loudly, from the insert -- which is the right outcome: appending a tape
    ' with different columns to an existing pool should stop, not reshape it.
    function _create_sql(table, df, if_not_exists)
        cols = []
        for each c in schema(df)
            append(cols, "  \"" + c.column + "\" " + c.type)
        end for
        head = "create table "
        if if_not_exists then
            head = "create table if not exists "
        end if
        return head + "\"" + table + "\" (" + chr(10) + join(cols, "," + chr(10)) + chr(10) + ")"
    end function

    ' --------------------------------------------------------------- loading

    ' Create the table and insert every row. Returns
    ' { ok, message, table, rows, schema, sql }.
    '
    ' `replace: true` drops an existing table first. Without it, loading twice
    ' appends — which is correct for a growing pool and wrong for a re-run, so
    ' the choice is the caller's and has no default that silently doubles a
    ' total.
    function to_table(df, db, table, options)
        if not safe_name(table) then
            return { ok: false, message: "unsafe table name: " + table,
                     table: table, rows: 0, schema: [], sql: "" }
        end if
        sch = schema(df)
        for each c in sch
            if not safe_name(c.column) then
                return { ok: false,
                         message: "column " + c.name + " has no safe identifier (got \"" + c.column + "\")",
                         table: table, rows: 0, schema: sch, sql: "" }
            end if
        end for
        ' Two source columns that collapse to one identifier would silently
        ' overwrite each other, so that is refused too.
        seen = { }
        for each c in sch
            if has(seen, c.column) then
                return { ok: false,
                         message: "columns " + seen[c.column] + " and " + c.name + " both become \"" + c.column + "\"",
                         table: table, rows: 0, schema: sch, sql: "" }
            end if
            seen[c.column] = c.name
        end for

        do_replace = false
        if has(options, "replace") then
            do_replace = options.replace
        end if
        if do_replace then
            sqlite.exec(db, "drop table if exists \"" + table + "\"")
        end if

        ddl = create_sql(table, df)
        sqlite.exec(db, _create_sql(table, df, not do_replace))

        names = []
        marks = []
        for each c in sch
            append(names, "\"" + c.column + "\"")
            append(marks, "?")
        end for
        ins = "insert into \"" + table + "\" (" + join(names, ", ") + ") values (" + join(marks, ", ") + ")"

        n = 0
        for each row in frame.to_rows(df)
            vals = []
            for each c in sch
                append(vals, row[c.name])
            end for
            ' BOUND, never interpolated: the values are data from someone
            ' else's spreadsheet.
            sqlite.exec(db, ins, vals)
            n = n + 1
        end for

        return { ok: true, message: "", table: table, rows: n, schema: sch, sql: ddl }
    end function

end library
