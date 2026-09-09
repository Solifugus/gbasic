' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' discovery -- what a database ESTATE says about itself. See
' docs/discovery_design.md.
'
' THIS INCREMENT READS DECLARED FACTS ONLY. Tables, columns, types,
' nullability, primary keys and foreign keys, read from the catalog through the
' driver manager. NOTHING HERE INFERS ANYTHING, and that is the design rather
' than a stopping point: an inferred relationship is the result of a SEARCH,
' and a search over a 500-table estate is ~50 million candidate pairs where
' coincidences are a certainty rather than a risk. Inference arrives with a
' null model or not at all.
'
' EVERY IDENTITY RULE BELOW WAS MEASURED against four drivers on 2026-09-08,
' not reasoned about. The comments say which fact forced which decision,
' because each of them is invisible from any single database.
library discovery

    load odbc

    ' The key separator. A dotted qualified name is what a human and a model
    ' both want to read, so an identifier CONTAINING a dot would make one key
    ' mean two things -- refused where it is read rather than escaped, which is
    ' the rule `dbframe` already follows for identifiers.
    function _sep()
        return "."
    end function

    function _part(row, field)
        ' A catalog column that the driver leaves empty comes back as `nothing`
        ' (MariaDB has no schema, SQLite has neither qualifier), and `unknown`
        ' if the driver omitted the column entirely. Both mean "this database
        ' does not qualify things that way" and both must render as empty
        ' rather than the word "nothing".
        if not has(row, field) then
            return ""
        end if
        v = row[field]
        if is_nothing(v) or is_unknown(v) then
            return ""
        end if
        return string(v)
    end function

    function _check_name(name, what)
        if contains(name, _sep()) then
            error "discovery: the " + what + " '" + name + "' contains a '" + _sep() + "', which is the key separator; this estate cannot be keyed unqualified"
        end if
        return nothing
    end function

    ' A stable id. THE QUALIFIER IS NOT IN THE SAME FIELD ON EVERY DATABASE:
    ' MariaDB puts the database in TABLE_CAT and leaves TABLE_SCHEM empty,
    ' PostgreSQL and SQL Server populate both, SQLite neither. So an id built
    ' as `schema.table` yields `.orders` on MariaDB -- silently, in a library
    ' whose whole job is identifying columns. Both qualifiers are carried, both
    ' may be empty, and `source` leads because an estate is several databases.
    function id_of(parts)
        keep = []
        for each p in [parts.source, parts.catalog, parts.schema, parts.table, parts.column]
            if is_string(p) and len(p) > 0 then
                append(keep, p)
            end if
        end for
        return join(keep, _sep())
    end function

    ' discovery.scan(connection, options) -> a catalog for ONE source.
    '
    ' NAMED `scan` AND NOT `read`: `read` is a built-in, and a library function
    ' sharing a built-in's name resolves to itself INSIDE the library and to the
    ' built-in outside it -- legal, and noted by the interpreter on every load.
    ' A name that has to be explained at each call site is the wrong name.
    ' options: { source, catalog, schema, table }  -- `source` is required,
    ' because an estate that cannot say WHICH database a column came from
    ' cannot describe an organisation that runs several.
    function scan(conn, options)
        if not is_record(options) or not has(options, "source") then
            error "discovery: scan needs a `source` naming which database this is"
        end if
        src = options.source
        if not is_string(src) or len(src) = 0 then
            error "discovery: `source` must be a non-empty string"
        end if
        _check_name(src, "source name")

        filter = {}
        for each f in ["catalog", "schema", "table"]
            if has(options, f) then
                filter[f] = options[f]
            end if
        end for

        cat = { source: src, tables: {}, columns: {}, primary_keys: [], edges: [] }

        for each t in odbc.tables(conn, filter)
            if _part(t, "TABLE_TYPE") = "TABLE" then
                rec = { source: src,
                        catalog: _part(t, "TABLE_CAT"),
                        schema: _part(t, "TABLE_SCHEM"),
                        table: _part(t, "TABLE_NAME"),
                        column: "",
                        type: _part(t, "TABLE_TYPE") }
                _check_name(rec.table, "table name")
                cat.tables[id_of(rec)] = rec
            end if
        end for

        for each c in odbc.columns(conn, filter)
            rec = { source: src,
                    catalog: _part(c, "TABLE_CAT"),
                    schema: _part(c, "TABLE_SCHEM"),
                    table: _part(c, "TABLE_NAME"),
                    column: _part(c, "COLUMN_NAME"),
                    position: c["ORDINAL_POSITION"],
                    type_name: _part(c, "TYPE_NAME"),
                    data_type: c["DATA_TYPE"],
                    ' NULLABILITY: the NUMERIC column, always. IS_NULLABLE is
                    ' EMPTY on PostgreSQL, and SQLite reports a PRIMARY KEY as
                    ' nullable, which is false -- so this field is recorded and
                    ' NOT trusted for inference, and the design says so.
                    nullable: c["NULLABLE"] }
            _check_name(rec.column, "column name")
            ' Only columns of tables we kept -- a view's columns are declared
            ' lineage and belong to a later increment, not to this one.
            owner = id_of({ source: src, catalog: rec.catalog, schema: rec.schema,
                            table: rec.table, column: "" })
            if has(cat.tables, owner) then
                cat.columns[id_of(rec)] = rec
            end if
        end for

        for each tid in keys(cat.tables)
            t = cat.tables[tid]
            spec = { table: t.table }
            if len(t.schema) > 0 then
                spec.schema = t.schema
            end if
            if len(t.catalog) > 0 then
                spec.catalog = t.catalog
            end if
            for each k in odbc.primary_keys(conn, spec)
                append(cat.primary_keys, id_of({ source: src,
                                                 catalog: _part(k, "TABLE_CAT"),
                                                 schema: _part(k, "TABLE_SCHEM"),
                                                 table: _part(k, "TABLE_NAME"),
                                                 column: _part(k, "COLUMN_NAME") }))
            end for
            for each f in odbc.foreign_keys(conn, spec)
                ' `enforced` is the ONLY kind this increment can produce: the
                ' database checks it and said so. Every other kind in the
                ' estate's vocabulary -- real_undeclared, real_indirect,
                ' real_non_equality, real_conditional -- requires inference,
                ' and inference requires a null model.
                append(cat.edges, { kind: "enforced",
                                    name: _part(f, "FK_NAME"),
                                    from: id_of({ source: src,
                                                  catalog: _part(f, "FKTABLE_CAT"),
                                                  schema: _part(f, "FKTABLE_SCHEM"),
                                                  table: _part(f, "FKTABLE_NAME"),
                                                  column: _part(f, "FKCOLUMN_NAME") }),
                                    to: id_of({ source: src,
                                                catalog: _part(f, "PKTABLE_CAT"),
                                                schema: _part(f, "PKTABLE_SCHEM"),
                                                table: _part(f, "PKTABLE_NAME"),
                                                column: _part(f, "PKCOLUMN_NAME") }) })
            end for
        end for
        return cat
    end function

    ' Several sources become one estate. THE WHOLE POINT of `source` being part
    ' of every identity: one organisation commonly runs Postgres, SQL Server
    ' and MySQL at once, and an id that does not name the database cannot tell
    ' two `orders` tables apart.
    function estate(catalogs)
        if not is_array(catalogs) then
            error "discovery: estate expects an array of catalogs"
        end if
        out = { sources: [], tables: {}, columns: {}, primary_keys: [], edges: [] }
        for each c in catalogs
            if contains(out.sources, c.source) then
                error "discovery: two catalogs both name themselves '" + c.source + "'; a source name must identify one database"
            end if
            append(out.sources, c.source)
            for each k in keys(c.tables)
                out.tables[k] = c.tables[k]
            end for
            for each k in keys(c.columns)
                out.columns[k] = c.columns[k]
            end for
            for each p in c.primary_keys
                append(out.primary_keys, p)
            end for
            for each e in c.edges
                append(out.edges, e)
            end for
        end for
        return out
    end function

    ' The columns of one table, in ordinal order.
    function columns_of(cat, table_id)
        out = []
        for each k in keys(cat.columns)
            c = cat.columns[k]
            owner = id_of({ source: c.source, catalog: c.catalog, schema: c.schema,
                            table: c.table, column: "" })
            if owner = table_id then
                append(out, c)
            end if
        end for
        ' ORDINAL ORDER, not the order keys() happened to yield: a column list
        ' whose order varies between reads cannot be compared to a later one,
        ' and comparing two reads is how "did the schema change" is answered.
        i = 0
        while i < count(out)
            j = i + 1
            while j < count(out)
                if number(out[j].position) < number(out[i].position) then
                    swap = out[i]
                    out[i] = out[j]
                    out[j] = swap
                end if
                j = j + 1
            end while
            i = i + 1
        end while
        return out
    end function

    ' Is this column part of its table's declared primary key?
    function is_primary_key(cat, column_id)
        return contains(cat.primary_keys, column_id)
    end function
end library
