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

    ' SYSTEM OBJECTS ARE EXCLUDED BY DEFAULT. Found the moment views were
    ' kept: a SQL Server scan went from 23 objects to 656, of which 17 were the
    ' business estate and the rest were sys.* and INFORMATION_SCHEMA.*. A
    ' catalog reporting `sys.all_columns` beside `trading.deal` is not
    ' describing the organisation, and every later search pays for the noise.
    ' `include_system: true` is the escape, since analysing the database itself
    ' is a legitimate thing to want.
    function _is_system_schema(s)
        return contains(["sys", "information_schema", "pg_catalog", "pg_toast",
                         "performance_schema", "mysql", "sysdiagrams"], lower(s))
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
        ' AN EXPLICIT "%" RATHER THAN AN ABSENT FILTER. ODBC says an omitted
        ' pattern means "any", and three of four drivers agree -- but FreeTDS
        ' against SQL Server refuses it outright ("sp_columns expects parameter
        ' '@table_name', which was not supplied"). MEASURED: every earlier test
        ' happened to pass a table filter, so a whole-schema scan was broken on
        ' one database and green on the rest. "%" says the same thing in a way
        ' every driver accepts.
        if not has(filter, "table") then
            filter.table = "%"
        end if
        ' AND AN EXPLICIT "%" FOR THE SCHEMA, for a worse reason than the
        ' table's. MEASURED: psqlODBC with no schema pattern returns only the
        ' schemas on the CURRENT SEARCH PATH -- so an estate in `trading`,
        ' `finance` and `warehouse` came back as 17 `public` tables with NO
        ' ERROR, a complete-looking answer that was missing most of the
        ' database. SQL Server refuses an omitted table pattern loudly; this
        ' one answers quietly and wrongly, which is the failure this library
        ' exists to avoid producing.
        if not has(filter, "schema") then
            filter.schema = "%"
        end if

        cat = { source: src, tables: {}, columns: {}, primary_keys: [], edges: [] }

        for each t in odbc.tables(conn, filter)
            ' VIEWS ARE KEPT, not only base tables. Found by pointing this at
            ' the estate: a view reading ANOTHER view (`disc_v2 -> disc_v1`)
            ' could not be resolved at all, because the resolution target was
            ' not in the catalog. A view is an object something reads FROM, and
            ' views on views are ordinary -- estate R11/R29 plants them because
            ' in a real schema the view is often the interface. The `type`
            ' field says which it is, so a caller that wants only base tables
            ' can still tell.
            kindof = _part(t, "TABLE_TYPE")
            sys_obj = _is_system_schema(_part(t, "TABLE_SCHEM"))
            if has(options, "include_system") then
                if options.include_system then
                    sys_obj = false
                end if
            end if
            if (kindof = "TABLE" or kindof = "VIEW") and not sys_obj then
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

    ' ---- reading SQL for the objects it touches ----------------------------
    '
    ' NOT A SQL PARSER, and the distinction is the design. Lineage needs four
    ' things -- which objects are READ, which are WRITTEN, and (later) the
    ' projection and the predicates -- and none of them requires understanding
    ' the statement. What IS required is not being fooled: a `from` inside a
    ' string literal or a comment is not a table, and a CTE name is not a table
    ' either.
    '
    ' WHY PARSING AT ALL, when a database offers dependency metadata: because
    ' that route fails. Reported from a working Python implementation against
    ' SQL Server -- the metadata approach failed repeatedly, changing between
    ' versions -- and corroborated by the product's own history, since
    ' sys.sql_dependencies was deprecated for its inability to cope with
    ' deferred name resolution. Parsing succeeded. Source text, meanwhile, is
    ' retrievable on every database measured.

    function _is_ident_byte(b)
        if b >= 48 and b <= 57 then
            return true
        end if
        if b >= 65 and b <= 90 then
            return true
        end if
        if b >= 97 and b <= 122 then
            return true
        end if
        ' _ $ # -- all legal in identifiers somewhere among these dialects
        return b = 95 or b = 36 or b = 35
    end function

    ' Tokens are { kind, text }: "word" (identifier or keyword, lowercased for
    ' matching), "quoted" (a delimited identifier, contents kept), "string" (a
    ' literal, contents DISCARDED -- nothing inside one is ever a reference),
    ' or "punct". Comments are dropped here rather than later, because a
    ' commented-out `insert` is exactly the sort of thing that produces a
    ' confident phantom edge.
    function _sql_tokens(sql)
        out = []
        i = 0
        n = byte_count(sql)
        while i < n
            b = byte_at(sql, i)
            if b = 45 and i + 1 < n and byte_at(sql, i + 1) = 45 then
                while i < n and byte_at(sql, i) != 10
                    i = i + 1
                end while
            else
                if b = 47 and i + 1 < n and byte_at(sql, i + 1) = 42 then
                    i = i + 2
                    while i + 1 < n and not (byte_at(sql, i) = 42 and byte_at(sql, i + 1) = 47)
                        i = i + 1
                    end while
                    i = i + 2
                else
                    if b = 39 then
                        i = i + 1
                        lit = []
                        while i < n
                            if byte_at(sql, i) = 39 then
                                if i + 1 < n and byte_at(sql, i + 1) = 39 then
                                    append(lit, "'")
                                    i = i + 2
                                else
                                    i = i + 1
                                    break
                                end if
                            else
                                append(lit, from_bytes([byte_at(sql, i)]))
                                i = i + 1
                            end if
                        end while
                        ' THE CONTENTS ARE KEPT, though nothing inside a literal
                        ' is ever an object reference -- the `string` kind is
                        ' what prevents that, and the walker only ever reads
                        ' `word` and `quoted`. They are kept because in a
                        ' PREDICATE the literal IS the explanation: "status =
                        ' 'ACTIVE'" is the answer to why two numbers differ,
                        ' and "status = '...'" is not.
                        append(out, { kind: "string", text: join(lit, "") })
                    else
                        if b = 91 or b = 34 or b = 96 then
                            closer = 93
                            if b = 34 then
                                closer = 34
                            end if
                            if b = 96 then
                                closer = 96
                            end if
                            i = i + 1
                            piece = []
                            while i < n and byte_at(sql, i) != closer
                                append(piece, from_bytes([byte_at(sql, i)]))
                                i = i + 1
                            end while
                            i = i + 1
                            append(out, { kind: "quoted", text: join(piece, "") })
                        else
                            if b >= 48 and b <= 57 then
                                ' A NUMBER IS NOT A NAME, and until column
                                ' lineage tried to resolve one, nothing here
                                ' could tell them apart: digits are legal
                                ' inside an identifier, so `0.97` arrived as
                                ' the three tokens `0` `.` `97` and read
                                ' exactly like a qualified column. `_sources_in`
                                ' had been reporting `1` as a source column of
                                ' `x * (1 - y)` since it was written, harmlessly
                                ' until something tried to look it up -- and
                                ' then as an entry in the UNRESOLVED list,
                                ' which is the one field a caller has to be
                                ' able to trust. An identifier cannot begin
                                ' with a digit in any of these dialects, so
                                ' the leading byte settles it.
                                piece = []
                                while i < n and byte_at(sql, i) >= 48 and byte_at(sql, i) <= 57
                                    append(piece, from_bytes([byte_at(sql, i)]))
                                    i = i + 1
                                end while
                                if i + 1 < n and byte_at(sql, i) = 46 and byte_at(sql, i + 1) >= 48 and byte_at(sql, i + 1) <= 57 then
                                    append(piece, ".")
                                    i = i + 1
                                    while i < n and byte_at(sql, i) >= 48 and byte_at(sql, i) <= 57
                                        append(piece, from_bytes([byte_at(sql, i)]))
                                        i = i + 1
                                    end while
                                end if
                                append(out, { kind: "number", text: join(piece, "") })
                            else
                            if _is_ident_byte(b) then
                                piece = []
                                while i < n and _is_ident_byte(byte_at(sql, i))
                                    append(piece, from_bytes([byte_at(sql, i)]))
                                    i = i + 1
                                end while
                                append(out, { kind: "word", text: lower(join(piece, "")) })
                            else
                                if b > 32 then
                                    append(out, { kind: "punct", text: from_bytes([b]) })
                                end if
                                i = i + 1
                            end if
                            end if
                        end if
                    end if
                end if
            end if
        end while
        return out
    end function

    ' A NAME THAT IS A KEYWORD IS NOT A NAME. Found by testing: MERGE's
    ' `when matched then update set d.n = s.n` has `update` followed by `set`,
    ' and taking the next word blindly emitted a table called `set`. A phantom
    ' object in a lineage graph is not a cosmetic error -- it is an edge to
    ' something that does not exist, reported with the same confidence as a
    ' real one.
    function _is_keyword(w)
        return contains(["select", "set", "where", "values", "from", "join", "into",
                         "update", "delete", "insert", "on", "and", "or", "as",
                         "when", "then", "matched", "not", "by", "group", "order",
                         "having", "union", "all", "distinct", "exec", "execute",
                         "with", "case", "if", "begin", "end", "declare", "return",
                         "using", "inner", "left", "right", "full", "outer", "cross",
                         "apply", "top", "output", "table", "only", "default"], w)
    end function

    ' A possibly-qualified name starting at `at`. Returns { name, next } with
    ' name empty when what follows is not a name at all -- `from (select ...)`
    ' being the case that matters, since a subquery is not a table and emitting
    ' one as a phantom is exactly the confident-wrong-answer failure.
    function _name_at(toks, at)
        parts = []
        i = at
        while i < count(toks)
            tk = toks[i]
            if tk.kind = "word" or tk.kind = "quoted" then
                append(parts, tk.text)
                i = i + 1
                if i < count(toks) and toks[i].kind = "punct" and toks[i].text = "." then
                    i = i + 1
                else
                    break
                end if
            else
                break
            end if
        end while
        return { name: join(parts, "."), next: i }
    end function

    ' discovery.references(sql) -> { reads, writes, gaps }
    '
    ' `gaps` is the load-bearing field. Dynamic SQL assembled at run time is
    ' not a parse failure to be guessed past: it is a hop that CANNOT be read,
    ' and a tracer that drops it silently produces a lineage graph that is
    ' confidently incomplete -- worse than one that names what it could not
    ' follow.
    function _references_range(toks, from_i, to_i)
        reads = []
        writes = []
        gaps = []
        ctes = []

        ' A CTE name is `word as (`. A table alias is `word as word`, and a
        ' derived table is `) as word`, so the shape is specific enough.
        i = from_i
        while i + 2 < to_i
            if toks[i].kind = "word" and toks[i + 1].kind = "word" and toks[i + 1].text = "as" then
                if toks[i + 2].kind = "punct" and toks[i + 2].text = "(" then
                    append(ctes, toks[i].text)
                end if
            end if
            i = i + 1
        end while

        prev = ""
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "word" then
                w = tk.text
                target = ""
                if w = "from" then
                    ' DELETE FROM t is a WRITE, and it is the one place `from`
                    ' does not mean a read.
                    if prev = "delete" then
                        target = "write"
                    else
                        target = "read"
                    end if
                end if
                if w = "join" then
                    target = "read"
                end if
                ' MERGE ... USING t: the source side is a read, and it is the
                ' only place a read is introduced by neither `from` nor `join`.
                if w = "using" then
                    target = "read"
                end if
                if w = "into" then
                    ' insert into / merge into / select ... into -- all writes.
                    target = "write"
                end if
                if w = "update" or w = "merge" then
                    target = "write"
                end if
                if w = "table" and (prev = "truncate" or prev = "drop" or prev = "alter") then
                    target = "write"
                end if
                if w = "sp_executesql" or w = "immediate" then
                    append(gaps, "dynamic SQL: " + w)
                end if
                if (w = "exec" or w = "execute") and i + 1 < to_i then
                    if toks[i + 1].kind = "punct" and toks[i + 1].text = "(" then
                        append(gaps, "dynamic SQL: exec(...)")
                    end if
                end if
                if len(target) > 0 then
                    found = _name_at(toks, i + 1)
                    nm = found.name
                    first = lower(split(nm, ".")[0])
                    if len(nm) > 0 and not contains(ctes, lower(nm)) and not _is_keyword(first) then
                        if target = "read" then
                            if not contains(reads, nm) then
                                append(reads, nm)
                            end if
                        else
                            if not contains(writes, nm) then
                                append(writes, nm)
                            end if
                            ' AN UPDATE READS THE TABLE IT WRITES, and so does
                            ' a MERGE: `set x = y * 0.97 where status = 'A'`
                            ' takes both `y` and `status` from the target
                            ' itself. Reported as a write alone, a restatement
                            ' looks like a module with no inputs -- so altering
                            ' the column it reads would show no impact on it.
                            ' A DELETE is deliberately not in this: it reads
                            ' the table to choose rows and puts no value into
                            ' anything.
                            if w = "update" or w = "merge" or (w = "into" and prev = "merge") then
                                if not contains(reads, nm) then
                                    append(reads, nm)
                                end if
                            end if
                        end if
                    end if
                end if
                prev = w
            else
                if tk.kind != "punct" or tk.text != "." then
                    prev = ""
                end if
            end if
            i = i + 1
        end while
        return { reads: reads, writes: writes, gaps: gaps }
    end function

    ' discovery.references(sql) -> { reads, writes, gaps } over a WHOLE body.
    function references(sql)
        toks = _sql_tokens(sql)
        return _references_range(toks, 0, count(toks))
    end function

    ' ---- modules: the views and procedures, WITH THEIR OWNING SCHEMA -------
    '
    ' READING MODULE SOURCE HAS NO PORTABLE CALL. SQLTables and SQLColumns are
    ' normalised by the driver manager; nothing equivalent exists for source
    ' text, so the query is per-dialect and the dialect is asked of the
    ' DATABASE (`odbc.info`) rather than guessed from the driver name -- one
    ' driver reaches several products and one product has several drivers.
    '
    ' THE SCHEMA IS NOT DECORATION. An unqualified reference inside a module
    ' resolves relative to the module's own container: MEASURED on SQL Server,
    ' a procedure in schema `alt` referencing bare `res_probe` binds to
    ' `alt.res_probe` when both it and `dbo.res_probe` exist, and falls back to
    ' `dbo` when only that one does. The stored source reads `from res_probe`
    ' either way, so without the owning schema the reference cannot be bound at
    ' all.
    function _module_sql(dbms)
        if contains(dbms, "SQLite") then
            ' No stored procedures at all, and no schemas.
            return ["select '' as mod_schema, name as mod_name, 'VIEW' as mod_kind, sql as mod_body from sqlite_master where type = 'view' and sql is not null"]
        end if
        if contains(dbms, "MariaDB") or contains(dbms, "MySQL") then
            ' No schemas -- what information_schema calls table_schema IS the
            ' database, which is why the estate's identity carries a catalog
            ' AND a schema and lets either be empty.
            return ["select table_schema as mod_schema, table_name as mod_name, 'VIEW' as mod_kind, view_definition as mod_body from information_schema.views where table_schema = database()",
                    "select routine_schema as mod_schema, routine_name as mod_name, routine_type as mod_kind, routine_definition as mod_body from information_schema.routines where routine_schema = database()"]
        end if
        if contains(dbms, "PostgreSQL") then
            return ["select table_schema as mod_schema, table_name as mod_name, 'VIEW' as mod_kind, view_definition as mod_body from information_schema.views where table_schema not in ('pg_catalog', 'information_schema')",
                    "select n.nspname as mod_schema, p.proname as mod_name, 'FUNCTION' as mod_kind, p.prosrc as mod_body from pg_proc p join pg_namespace n on n.oid = p.pronamespace where n.nspname not in ('pg_catalog', 'information_schema')"]
        end if
        if contains(dbms, "SQL Server") then
            ' sys.sql_modules rather than information_schema.views: the latter
            ' truncates a long definition, and a lineage read that silently
            ' loses the tail of a procedure is the confidently-incomplete
            ' failure this library is arranged against.
            return ["select schema_name(o.schema_id) as mod_schema, o.name as mod_name, o.type_desc as mod_kind, m.definition as mod_body from sys.sql_modules m join sys.objects o on o.object_id = m.object_id"]
        end if
        error "discovery: no module reader for '" + dbms + "'; views and procedures are read per-dialect and this one is not known"
    end function

    ' discovery.modules(connection, { source }) -> array of
    '   { source, schema, name, kind, body }
    function modules(conn, options)
        if not is_record(options) or not has(options, "source") then
            error "discovery: modules needs a `source` naming which database this is"
        end if
        src = options.source
        info = odbc.info(conn)
        out = []
        for each q in _module_sql(info.dbms_name)
            for each r in odbc.query(conn, q)
                body = r["mod_body"]
                if is_string(body) and len(trim(body)) > 0 then
                    append(out, { source: src,
                                  schema: _part(r, "mod_schema"),
                                  name: _part(r, "mod_name"),
                                  kind: _part(r, "mod_kind"),
                                  body: body })
                end if
            end for
        end for
        return out
    end function

    ' ---- the projection, and the predicates --------------------------------
    '
    ' THIS IS WHAT ANSWERS THE QUESTION PEOPLE ACTUALLY ASK. Two reports both
    ' show a number called `revenue` and they differ; today that costs someone
    ' a day of manual research. Both are correct. The answer is never "these
    ' are unrelated" -- it is "this one excludes cancelled orders", or "this
    ' one books at order date and that one at ship date".
    '
    ' So THE PREDICATE IS PART OF THE LINEAGE, not metadata about it: a `where`
    ' clause IS the reason two numbers differ. A tool that reports both columns
    ' derive from the same table has said something true and answered nothing.

    function _kw_at(toks, i, w)
        return toks[i].kind = "word" and toks[i].text = w
    end function

    ' Reconstructed rather than sliced from the source: the token stream has no
    ' byte offsets, and a NORMALISED rendering is better here anyway, because
    ' two expressions are being COMPARED and formatting differences would read
    ' as real ones.
    function _render(toks, from_i, to_i)
        out = []
        i = from_i
        while i < to_i
            tk = toks[i]
            txt = tk.text
            if tk.kind = "string" then
                txt = "'" + txt + "'"
            end if
            if tk.kind = "quoted" then
                txt = "\"" + txt + "\""
            end if
            ' SPACING IS PART OF THE ANSWER, because this text is READ BY A
            ' PERSON asking why two numbers differ. Joining every token with a
            ' space renders `0.97` as `0 . 97` and `sum(x)` as `sum ( x )`,
            ' which is legible only to someone who already knows what it says.
            glue = false
            if i > from_i then
                pv = toks[i - 1]
                if tk.kind = "punct" and contains([".", ",", ")"], tk.text) then
                    glue = true
                end if
                if pv.kind = "punct" and contains([".", "("], pv.text) then
                    glue = true
                end if
                ' A call binds to its name; `in (1, 2)` does not.
                if tk.kind = "punct" and tk.text = "(" then
                    if pv.kind = "word" and not _is_keyword(pv.text) then
                        glue = true
                    end if
                end if
            end if
            if glue and count(out) > 0 then
                out[count(out) - 1] = out[count(out) - 1] + txt
            else
                append(out, txt)
            end if
            i = i + 1
        end while
        return join(out, " ")
    end function

    ' The columns an expression reads: every word that is not a keyword and is
    ' not the name of a function being called. A qualified `s.amount` yields
    ' `amount`, because the qualifier is a table alias and aliases are local to
    ' the statement.
    function _sources_in(toks, from_i, to_i)
        out = []
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "word" or tk.kind = "quoted" then
                is_call = false
                if i + 1 < to_i then
                    if toks[i + 1].kind = "punct" and toks[i + 1].text = "(" then
                        is_call = true
                    end if
                end if
                qualifier = false
                if i + 1 < to_i then
                    if toks[i + 1].kind = "punct" and toks[i + 1].text = "." then
                        qualifier = true
                    end if
                end if
                if not is_call and not qualifier and not _is_keyword(tk.text) then
                    if not contains(out, tk.text) then
                        append(out, tk.text)
                    end if
                end if
            end if
            i = i + 1
        end while
        return out
    end function

    ' discovery.projection(sql) -> array of { output, expression, sources, aggregate }
    '
    ' `output` is the column a consumer sees; `sources` are the columns it was
    ' built from. `select *` is NOT expanded and is reported as such: expanding
    ' it would require knowing the shape of something this function was not
    ' given, and guessing is how a lineage graph becomes confident and wrong.
    function _select_list(toks, from_i, to_i)
        out = []
        ' Find the first top-level SELECT and the FROM that closes its list.
        depth = 0
        start = -1
        list_end = -1
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" then
                if tk.text = "select" and start < 0 then
                    start = i + 1
                end if
                if tk.text = "from" and start >= 0 and list_end < 0 then
                    list_end = i
                end if
            end if
            i = i + 1
        end while
        if start < 0 then
            return out
        end if
        if list_end < 0 then
            list_end = to_i
        end if
        ' Split the list on top-level commas.
        item_start = start
        depth = 0
        i = start
        while i <= list_end
            done = i = list_end
            if not done then
                tk = toks[i]
                if tk.kind = "punct" and tk.text = "(" then
                    depth = depth + 1
                end if
                if tk.kind = "punct" and tk.text = ")" then
                    depth = depth - 1
                end if
                if depth = 0 and tk.kind = "punct" and tk.text = "," then
                    done = true
                end if
            end if
            if done and i > item_start then
                ' The output name: after `as`, else the last bare word, else
                ' the expression itself.
                name = ""
                expr_end = i
                j = item_start
                d2 = 0
                while j < i
                    if toks[j].kind = "punct" and toks[j].text = "(" then
                        d2 = d2 + 1
                    end if
                    if toks[j].kind = "punct" and toks[j].text = ")" then
                        d2 = d2 - 1
                    end if
                    ' ONLY A DEPTH-ZERO `as` IS AN ALIAS. `cast(x as int)` has
                    ' one that is part of the expression, and taking it cut
                    ' the expression off at the cast and named the output
                    ' column `int` -- a plausible name, on a truncated
                    ' derivation, with nothing raised.
                    if d2 = 0 and _kw_at(toks, j, "as") then
                        if j + 1 < i then
                            name = toks[j + 1].text
                        end if
                        expr_end = j
                    end if
                    j = j + 1
                end while
                if len(name) = 0 then
                    last = toks[i - 1]
                    if last.kind = "word" and not _is_keyword(last.text) then
                        name = last.text
                    end if
                    if last.kind = "quoted" then
                        name = last.text
                    end if
                    if last.kind = "punct" and last.text = "*" then
                        name = "*"
                    end if
                end if
                agg = false
                j = item_start
                while j < expr_end
                    if toks[j].kind = "word" then
                        if contains(["sum", "count", "avg", "min", "max", "stdev", "var"], toks[j].text) then
                            agg = true
                        end if
                    end if
                    j = j + 1
                end while
                append(out, { output: name,
                              expression: _render(toks, item_start, expr_end),
                              sources: _sources_in(toks, item_start, expr_end),
                              source_refs: _source_refs(toks, item_start, expr_end),
                              aggregate: agg })
                item_start = i + 1
            end if
            i = i + 1
        end while
        return out
    end function

    ' discovery.projection(sql) -> the SELECT list, wherever the first one is.
    '
    ' Kept as it was when it was the only shape this library read: a view is
    ' one select and `explain` compares two of them. A body that WRITES needs
    ' `derivations`, because the output names are on the other side of the
    ' statement and pairing them is a decision this function does not make.
    function projection(sql)
        toks = _sql_tokens(sql)
        return _select_list(toks, 0, count(toks))
    end function

    ' discovery.predicates(sql) -> array of { kind, text }
    '
    ' `where` and `having` narrow which rows contribute; a join `on` decides
    ' which rows pair up and can narrow just as effectively. All three belong
    ' to the derivation, because all three are answers to "why is this number
    ' different from that one".
    function _predicates_range(toks, from_i, to_i)
        out = []
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "word" and contains(["where", "having", "on"], tk.text) then
                kind = tk.text
                if kind = "on" then
                    kind = "join"
                end if
                j = i + 1
                depth = 0
                while j < to_i
                    t2 = toks[j]
                    if t2.kind = "punct" and t2.text = "(" then
                        depth = depth + 1
                    end if
                    if t2.kind = "punct" and t2.text = ")" then
                        if depth = 0 then
                            break
                        end if
                        depth = depth - 1
                    end if
                    if depth = 0 and t2.kind = "word" then
                        ' `when` and the DML verbs are in this list because a
                        ' MERGE puts its arms AFTER the `on`, so without them
                        ' the join predicate swallowed every arm of the
                        ' statement and reported the whole thing as one
                        ' condition.
                        if contains(["where", "group", "order", "having", "join", "union", "select",
                                     "inner", "left", "right", "full", "cross", "when", "then",
                                     "values", "set", "using", "merge", "insert", "update",
                                     "delete", "output", "returning"], t2.text) then
                            break
                        end if
                    end if
                    j = j + 1
                end while
                if j > i + 1 then
                    append(out, { kind: kind, text: _render(toks, i + 1, j) })
                end if
                i = j
            else
                i = i + 1
            end if
        end while
        return out
    end function

    ' discovery.predicates(sql) -> array of { kind, text } over a WHOLE body.
    function predicates(sql)
        toks = _sql_tokens(sql)
        return _predicates_range(toks, 0, count(toks))
    end function

    ' ---- statements: a procedure body is not one statement -----------------
    '
    ' Everything above this line reads ONE statement, which is all a view ever
    ' is. A stored procedure is where the ETL actually lives, and there the
    ' interesting facts are per statement: `truncate table x; insert into x
    ' select ... from y` writes x twice for two different reasons, and a
    ' function that found "the first select" would describe the second and
    ' silently drop the first.
    '
    ' SPLITTING IS NOT A SOLVED PROBLEM AND THIS DOES NOT PRETEND IT IS.
    ' T-SQL makes the semicolon optional, so a body may be several statements
    ' with nothing between them, and deciding where one ends needs a grammar
    ' this library does not have. What it has instead is a rule about verbs:
    ' `insert`, `update`, `delete`, `merge` and the rest START a statement
    ' when they appear at bracket depth zero -- with three exceptions, each of
    ' which was a wrong answer before it was an exception.
    function _stmt_starters()
        return ["insert", "update", "delete", "merge", "truncate", "declare",
                "exec", "execute", "print", "create", "drop", "alter", "with",
                "if", "while", "select", "return", "grant", "revoke"]
    end function

    function _statement_ranges(toks)
        out = []
        n = count(toks)
        depth = 0
        start = 0
        prev = ""
        first_word = ""
        seen_select = false
        seen_verb = false
        i = 0
        while i < n
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "punct" and tk.text = ";" then
                if i > start then
                    append(out, { from: start, to: i })
                end if
                start = i + 1
                first_word = ""
                seen_select = false
                seen_verb = false
                prev = ""
            else
                boundary = false
                if depth = 0 and tk.kind = "word" and i > start then
                    if contains(_stmt_starters(), tk.text) then
                        boundary = true
                        ' EXCEPTION 1: `... then insert` / `... then update`.
                        ' A MERGE arm and a T-SQL `if ... then` both put a verb
                        ' where it does not begin anything.
                        if prev = "then" then
                            boundary = false
                        end if
                        ' EXCEPTION 2: a MERGE owns every arm it declares. This
                        ' is the one that produced a phantom table the first
                        ' time it was missing -- `when matched then update set`
                        ' split into a statement whose target was `set`.
                        if first_word = "merge" then
                            boundary = false
                        end if
                        ' EXCEPTION 2b: a CTE belongs to the statement it
                        ' feeds. Split off, `with c as (...) insert into t
                        ' select x from c` leaves an insert that reads a table
                        ' called `c` -- a phantom object, reported with
                        ' exactly the confidence of a real one.
                        if first_word = "with" and not seen_verb then
                            if contains(["insert", "update", "delete", "merge", "select"], tk.text) then
                                boundary = false
                            end if
                        end if
                        if tk.text = "select" then
                            ' EXCEPTION 3: the SELECT that FEEDS a write is
                            ' part of that write. `insert into t (a) select x`
                            ' is one statement, and treating its select as a
                            ' second one loses the pairing that makes column
                            ' lineage possible at all.
                            if contains(["insert", "update", "delete", "merge", "with",
                                         "create", "declare", "if", "while", "return"], first_word) then
                                if not seen_select then
                                    boundary = false
                                end if
                            end if
                            ' ... and a set operation is one statement with
                            ' several selects in it.
                            if contains(["union", "except", "intersect", "all", "as",
                                         "exists", "in", "then", "into"], prev) then
                                boundary = false
                            end if
                        end if
                    end if
                end if
                if boundary then
                    append(out, { from: start, to: i })
                    start = i
                    first_word = tk.text
                    seen_select = tk.text = "select"
                    seen_verb = contains(["insert", "update", "delete", "merge", "select"], tk.text)
                    prev = tk.text
                else
                    if tk.kind = "word" then
                        if i = start then
                            first_word = tk.text
                        end if
                        if depth = 0 and tk.text = "select" then
                            seen_select = true
                        end if
                        if depth = 0 and contains(["insert", "update", "delete", "merge", "select"], tk.text) then
                            seen_verb = true
                        end if
                        prev = tk.text
                    else
                        if tk.kind != "punct" or tk.text != "." then
                            prev = ""
                        end if
                    end if
                end if
            end if
            i = i + 1
        end while
        if n > start then
            append(out, { from: start, to: n })
        end if
        return out
    end function

    ' THE VERB, NOT THE FIRST WORD. `create view v as select ...` and
    ' `with c as (...) insert into t select ...` both begin with something that
    ' is not the verb, and the verb is what says which side of the statement
    ' carries the output names.
    function _statement_kind(toks, from_i, to_i)
        depth = 0
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" then
                if contains(["insert", "update", "delete", "merge", "truncate", "select"], tk.text) then
                    return { kind: tk.text, at: i }
                end if
            end if
            i = i + 1
        end while
        return { kind: "other", at: from_i }
    end function

    ' The word following `after` at depth zero, as a possibly-qualified name.
    function _name_after(toks, from_i, to_i, after)
        depth = 0
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" and tk.text = after then
                found = _name_at(toks, i + 1)
                if len(found.name) > 0 then
                    if not _is_keyword(lower(split(found.name, ".")[0])) then
                        return found.name
                    end if
                end if
                return ""
            end if
            i = i + 1
        end while
        return ""
    end function

    function _statement_target(toks, from_i, to_i, kind, at)
        if kind = "insert" then
            return _name_after(toks, from_i, to_i, "into")
        end if
        if kind = "update" then
            found = _name_at(toks, at + 1)
            if len(found.name) > 0 and not _is_keyword(lower(split(found.name, ".")[0])) then
                return found.name
            end if
            return ""
        end if
        if kind = "delete" then
            t = _name_after(toks, from_i, to_i, "from")
            if len(t) > 0 then
                return t
            end if
            found = _name_at(toks, at + 1)
            if len(found.name) > 0 and not _is_keyword(lower(split(found.name, ".")[0])) then
                return found.name
            end if
            return ""
        end if
        if kind = "merge" then
            t = _name_after(toks, from_i, to_i, "into")
            if len(t) > 0 then
                return t
            end if
            found = _name_at(toks, at + 1)
            if len(found.name) > 0 and not _is_keyword(lower(split(found.name, ".")[0])) then
                return found.name
            end if
            return ""
        end if
        if kind = "truncate" then
            return _name_after(toks, from_i, to_i, "table")
        end if
        if kind = "select" then
            ' `select ... into t` -- the one select that writes.
            return _name_after(toks, from_i, to_i, "into")
        end if
        return ""
    end function

    ' discovery.statements(sql) -> array of { kind, target, sql }
    function statements(sql)
        toks = _sql_tokens(sql)
        out = []
        for each r in _statement_ranges(toks)
            k = _statement_kind(toks, r.from, r.to)
            append(out, { kind: k.kind,
                          target: _statement_target(toks, r.from, r.to, k.kind, k.at),
                          sql: _render(toks, r.from, r.to) })
        end for
        return out
    end function

    ' ---- pairing an output column with the expression that fills it --------

    ' The column list of an INSERT, which may be written with or without
    ' `into`: MERGE's insert arm has no `into` at all.
    function _insert_columns(toks, from_i, to_i, at)
        out = []
        i = at + 1
        if i < to_i then
            if toks[i].kind = "word" and toks[i].text = "into" then
                i = i + 1
            end if
        end if
        found = _name_at(toks, i)
        j = found.next
        if len(found.name) = 0 then
            j = i
        end if
        if j >= to_i then
            return out
        end if
        if not (toks[j].kind = "punct" and toks[j].text = "(") then
            return out
        end if
        j = j + 1
        while j < to_i
            tk = toks[j]
            if tk.kind = "punct" and tk.text = ")" then
                break
            end if
            if tk.kind = "word" or tk.kind = "quoted" then
                append(out, tk.text)
            end if
            j = j + 1
        end while
        return out
    end function

    ' `values (a, b, c)` -- the other thing an INSERT can be fed by.
    function _values_list(toks, from_i, to_i)
        out = []
        i = from_i
        open_at = -1
        while i < to_i
            if toks[i].kind = "word" and toks[i].text = "values" then
                if i + 1 < to_i and toks[i + 1].kind = "punct" and toks[i + 1].text = "(" then
                    open_at = i + 1
                    break
                end if
            end if
            i = i + 1
        end while
        if open_at < 0 then
            return out
        end if
        item_start = open_at + 1
        depth = 0
        i = item_start
        while i < to_i
            tk = toks[i]
            done = false
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                if depth = 0 then
                    done = true
                else
                    depth = depth - 1
                end if
            end if
            if depth = 0 and tk.kind = "punct" and tk.text = "," then
                done = true
            end if
            if done then
                if i > item_start then
                    append(out, { output: "",
                                  expression: _render(toks, item_start, i),
                                  sources: _sources_in(toks, item_start, i),
                                  source_refs: _source_refs(toks, item_start, i),
                                  aggregate: false })
                end if
                item_start = i + 1
                if tk.kind = "punct" and tk.text = ")" then
                    break
                end if
            end if
            i = i + 1
        end while
        return out
    end function

    ' `set a = expr, b = expr` -- an UPDATE names both sides itself, which is
    ' the one shape where no pairing decision has to be made.
    function _set_list(toks, from_i, to_i)
        out = []
        set_at = -1
        depth = 0
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" and tk.text = "set" and set_at < 0 then
                set_at = i
            end if
            i = i + 1
        end while
        if set_at < 0 then
            return out
        end if
        stop_at = to_i
        depth = 0
        i = set_at + 1
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" then
                if contains(["from", "where", "output", "returning", "when"], tk.text) then
                    stop_at = i
                    break
                end if
            end if
            i = i + 1
        end while
        item_start = set_at + 1
        depth = 0
        i = item_start
        while i <= stop_at
            done = i = stop_at
            if not done then
                tk = toks[i]
                if tk.kind = "punct" and tk.text = "(" then
                    depth = depth + 1
                end if
                if tk.kind = "punct" and tk.text = ")" then
                    depth = depth - 1
                end if
                if depth = 0 and tk.kind = "punct" and tk.text = "," then
                    done = true
                end if
            end if
            if done and i > item_start then
                eq = -1
                j = item_start
                while j < i
                    if toks[j].kind = "punct" and toks[j].text = "=" then
                        eq = j
                        break
                    end if
                    j = j + 1
                end while
                if eq > item_start then
                    append(out, { output: toks[eq - 1].text,
                                  expression: _render(toks, eq + 1, i),
                                  sources: _sources_in(toks, eq + 1, i),
                                  source_refs: _source_refs(toks, eq + 1, i),
                                  aggregate: false })
                end if
                item_start = i + 1
            end if
            i = i + 1
        end while
        return out
    end function

    ' THE SAME COLUMNS AS `_sources_in`, WITH THE QUALIFIER KEPT. Two fields
    ' rather than one because they answer different questions and one answer
    ' is wrong for the other: `explain` compares two derivations and must see
    ' `f.amount` and `g.amount` as the SAME column of the same table, while
    ' lineage has to know which table `f` was, and in a two-table join the
    ' bare name cannot say.
    function _source_refs(toks, from_i, to_i)
        out = []
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "word" or tk.kind = "quoted" then
                is_call = false
                qualifier = ""
                if i + 1 < to_i then
                    if toks[i + 1].kind = "punct" and toks[i + 1].text = "(" then
                        is_call = true
                    end if
                    if toks[i + 1].kind = "punct" and toks[i + 1].text = "." then
                        is_call = true
                    end if
                end if
                if i >= from_i + 2 then
                    if toks[i - 1].kind = "punct" and toks[i - 1].text = "." then
                        if toks[i - 2].kind = "word" or toks[i - 2].kind = "quoted" then
                            qualifier = toks[i - 2].text
                        end if
                    end if
                end if
                if not is_call and not _is_keyword(tk.text) then
                    seen = false
                    for each r in out
                        if r.name = tk.text and r.qualifier = qualifier then
                            seen = true
                        end if
                    end for
                    if not seen then
                        append(out, { name: tk.text, qualifier: qualifier })
                    end if
                end if
            end if
            i = i + 1
        end while
        return out
    end function

    ' The objects a statement reads, WITH the alias each was given. Column
    ' lineage needs this and object lineage did not: `s.gross_vol_mmbtu` names
    ' a column of whatever `s` was bound to, and nothing else in the statement
    ' says which table that is.
    '
    ' The INSERT target is deliberately NOT among the objects: its columns are
    ' not in scope on the source side, and admitting it would let a column
    ' resolve to the very table being written -- a lineage edge from a column
    ' to itself, which looks exactly like a real one.
    function _aliases_in(toks, from_i, to_i)
        names = {}
        objects = []
        i = from_i
        while i < to_i
            tk = toks[i]
            trig = false
            reads = false
            if tk.kind = "word" then
                if contains(["from", "join", "using", "update", "merge", "into"], tk.text) then
                    trig = true
                    reads = tk.text != "into"
                end if
            end if
            if trig then
                found = _name_at(toks, i + 1)
                nm = found.name
                j = found.next
                if len(nm) > 0 and not _is_keyword(lower(split(nm, ".")[0])) then
                    if reads and not contains(objects, nm) then
                        append(objects, nm)
                    end if
                    if j < to_i then
                        if toks[j].kind = "word" and toks[j].text = "as" then
                            j = j + 1
                        end if
                    end if
                    if j < to_i then
                        alias_ok = false
                        if toks[j].kind = "quoted" then
                            alias_ok = true
                        end if
                        if toks[j].kind = "word" and not _is_keyword(toks[j].text) then
                            alias_ok = true
                        end if
                        if alias_ok then
                            names[toks[j].text] = nm
                        end if
                    end if
                end if
                i = found.next
            else
                i = i + 1
            end if
        end while
        return { alias: names, objects: objects }
    end function

    ' discovery.derivations(sql) -> one record per statement:
    '   { kind, target, columns, predicates, reads, writes, gaps, aliases,
    '     notes, sql }
    ' where a column is { output, expression, sources, source_refs, aggregate }.
    '
    ' `notes` IS NOT A DIAGNOSTIC TO BE IGNORED, for the same reason `gaps` is
    ' not: the two shapes that cannot be paired here -- an insert with no
    ' column list, and one whose counts disagree -- both produce a perfectly
    ' ordinary-looking set of expressions with the WRONG NAMES attached if
    ' paired anyway, and a column lineage built on that is confident and
    ' wrong in a way nothing downstream can detect.
    function derivations(sql)
        toks = _sql_tokens(sql)
        out = []
        for each r in _statement_ranges(toks)
            k = _statement_kind(toks, r.from, r.to)
            kind = k.kind
            cols = []
            notes = []
            ' HOW THE PAIRING WAS MADE, as a field rather than as prose a
            ' caller would have to sniff for. "ordinal" means the outputs are
            ' NOT named by this statement and the names in `columns` are the
            ' select list's own inferred ones, which are meaningless as target
            ' column names -- only the catalog can settle it.
            pairing = "none"
            if kind = "insert" then
                names = _insert_columns(toks, r.from, r.to, k.at)
                src = _select_list(toks, r.from, r.to)
                if count(src) = 0 then
                    src = _values_list(toks, r.from, r.to)
                end if
                if count(names) = 0 then
                    if count(src) > 0 then
                        append(notes, "no column list: which column each expression fills is decided by the target's ordinal order, which is a fact about the catalog and not about this statement")
                        pairing = "ordinal"
                    end if
                    cols = src
                else
                    if count(names) != count(src) then
                        append(notes, "the column list names " + string(count(names)) + " columns and the source produces " + string(count(src)) + "; they cannot be paired")
                        pairing = "refused"
                    else
                        pairing = "positional"
                        idx = 0
                        while idx < count(names)
                            c = src[idx]
                            append(cols, { output: names[idx], expression: c.expression,
                                           sources: c.sources, source_refs: c.source_refs,
                                           aggregate: c.aggregate })
                            idx = idx + 1
                        end while
                    end if
                end if
            end if
            if kind = "update" then
                cols = _set_list(toks, r.from, r.to)
                pairing = "assignment"
            end if
            if kind = "select" then
                cols = _select_list(toks, r.from, r.to)
                pairing = "select"
            end if
            if kind = "merge" then
                cols = _merge_arms(toks, r.from, r.to)
                pairing = "assignment"
            end if
            rr = _references_range(toks, r.from, r.to)
            append(out, { kind: kind,
                          target: _statement_target(toks, r.from, r.to, kind, k.at),
                          columns: cols,
                          pairing: pairing,
                          predicates: _predicates_range(toks, r.from, r.to),
                          reads: rr.reads,
                          writes: rr.writes,
                          gaps: rr.gaps,
                          aliases: _aliases_in(toks, r.from, r.to),
                          notes: notes,
                          sql: _render(toks, r.from, r.to) })
        end for
        return out
    end function

    ' A MERGE assigns from several arms into one target, so its columns are
    ' the union of them -- and a column assigned differently in the matched
    ' and not-matched arms genuinely has two derivations, which is reported as
    ' two entries rather than resolved into one.
    function _merge_arms(toks, from_i, to_i)
        out = []
        depth = 0
        i = from_i
        while i < to_i
            tk = toks[i]
            if tk.kind = "punct" and tk.text = "(" then
                depth = depth + 1
            end if
            if tk.kind = "punct" and tk.text = ")" then
                depth = depth - 1
            end if
            if depth = 0 and tk.kind = "word" and tk.text = "then" and i + 1 < to_i then
                arm = toks[i + 1].text
                arm_end = to_i
                d2 = 0
                j = i + 2
                while j < to_i
                    t2 = toks[j]
                    if t2.kind = "punct" and t2.text = "(" then
                        d2 = d2 + 1
                    end if
                    if t2.kind = "punct" and t2.text = ")" then
                        d2 = d2 - 1
                    end if
                    if d2 = 0 and t2.kind = "word" and t2.text = "when" then
                        arm_end = j
                        break
                    end if
                    j = j + 1
                end while
                if arm = "update" then
                    for each c in _set_list(toks, i + 1, arm_end)
                        append(out, c)
                    end for
                end if
                if arm = "insert" then
                    names = _insert_columns(toks, i + 1, arm_end, i + 1)
                    vals = _values_list(toks, i + 1, arm_end)
                    if count(names) = count(vals) and count(names) > 0 then
                        idx = 0
                        while idx < count(names)
                            c = vals[idx]
                            append(out, { output: names[idx], expression: c.expression,
                                          sources: c.sources, source_refs: c.source_refs,
                                          aggregate: false })
                            idx = idx + 1
                        end while
                    end if
                end if
                i = arm_end
            else
                i = i + 1
            end if
        end while
        return out
    end function

    ' discovery.explain(a, b) -> why two same-named columns differ.
    '
    ' `a` and `b` are { name, body } -- a column name and the SQL of the module
    ' producing it. THE QUESTION THIS LIBRARY EXISTS FOR: two reports both show
    ' a number called `total_volume`, they disagree, and someone spends a day
    ' finding out why. Both are correct. What is wanted is the point at which
    ' the two derivations parted.
    '
    ' It reports DIFFERENCES ONLY, and says so when it can find none: "these
    ' two are derived identically" is a real answer, and inventing a
    ' distinction to have something to say would be the failure this whole
    ' design is arranged against.
    function explain(a, b)
        pa = projection(a.body)
        pb = projection(b.body)
        ca = { output: "", expression: "", sources: [], aggregate: false }
        cb = ca
        found_a = false
        found_b = false
        for each c in pa
            if c.output = a.name then
                ca = c
                found_a = true
            end if
        end for
        for each c in pb
            if c.output = b.name then
                cb = c
                found_b = true
            end if
        end for
        if not found_a or not found_b then
            error "discovery: explain could not find both columns in their modules"
        end if
        ra = references(a.body)
        rb = references(b.body)
        shared = []
        for each x in ra.reads
            if contains(rb.reads, x) then
                append(shared, x)
            end if
        end for
        qa = predicates(a.body)
        qb = predicates(b.body)
        ta = []
        for each q in qa
            append(ta, q.kind + ": " + q.text)
        end for
        tb = []
        for each q in qb
            append(tb, q.kind + ": " + q.text)
        end for
        only_a = []
        for each x in ta
            if not contains(tb, x) then
                append(only_a, x)
            end if
        end for
        only_b = []
        for each x in tb
            if not contains(ta, x) then
                append(only_b, x)
            end if
        end for
        differences = []
        if ca.expression != cb.expression then
            append(differences, "the expression differs: " + ca.expression + "   versus   " + cb.expression)
        end if
        if join(ca.sources, ",") != join(cb.sources, ",") then
            append(differences, "they are built from different columns: [" + join(ca.sources, ", ") + "] versus [" + join(cb.sources, ", ") + "]")
        end if
        for each q in only_a
            append(differences, "only the first narrows on -- " + q)
        end for
        for each q in only_b
            append(differences, "only the second narrows on -- " + q)
        end for
        ' READ SIDES that differ change which rows exist at all, so they belong
        ' beside the predicates rather than under them.
        for each x in ra.reads
            if not contains(rb.reads, x) then
                append(differences, "only the first reads " + x)
            end if
        end for
        for each x in rb.reads
            if not contains(ra.reads, x) then
                append(differences, "only the second reads " + x)
            end if
        end for
        return { shared: shared,
                 differences: differences,
                 identical: count(differences) = 0 }
    end function

    ' ---- resolution: binding a bare name to a real object ------------------
    '
    ' PARSING AND RESOLUTION ARE SEPARATE PHASES. `references` produces names
    ' as written; this binds them using the catalog. MEASURED on SQL Server: a
    ' procedure in schema `alt` referencing bare `res_probe` binds to
    ' `alt.res_probe` when both it and `dbo.res_probe` exist, and to `dbo` when
    ' only that exists -- so the search path is [own schema, dbo]. The stored
    ' source reads `from res_probe` either way.
    function _search_path(dbms, own_schema)
        path = []
        if len(own_schema) > 0 then
            append(path, own_schema)
        end if
        if contains(dbms, "SQL Server") then
            if not contains(path, "dbo") then
                append(path, "dbo")
            end if
        end if
        if contains(dbms, "PostgreSQL") then
            if not contains(path, "public") then
                append(path, "public")
            end if
        end if
        return path
    end function

    ' Bind one reference. Returns the table id, or "" when it cannot be bound.
    ' AMBIGUITY IS NOT RESOLVED BY PICKING ONE: two equally good candidates
    ' mean the answer is unknown, and a lineage edge to a guess is worse than a
    ' recorded gap.
    function _bind(cat, reference, mod, path)
        parts = split(reference, ".")
        want = lower(parts[count(parts) - 1])
        hits = []
        for each tid in keys(cat.tables)
            tb = cat.tables[tid]
            if lower(tb.table) = want then
                ok = false
                if count(parts) = 1 then
                    ' Unqualified: the search path decides, in order.
                    if len(tb.schema) = 0 and len(path) = 0 then
                        ok = true
                    end if
                    if contains(path, tb.schema) then
                        ok = true
                    end if
                    if len(tb.schema) = 0 and len(tb.catalog) > 0 then
                        ' MariaDB has no schemas: the database is the qualifier.
                        ok = true
                    end if
                else
                    q = lower(parts[count(parts) - 2])
                    if lower(tb.schema) = q or lower(tb.catalog) = q then
                        ok = true
                    end if
                end if
                if ok then
                    append(hits, tid)
                end if
            end if
        end for
        if count(hits) = 1 then
            return hits[0]
        end if
        if count(hits) > 1 and count(parts) = 1 then
            ' Prefer the earliest schema on the search path -- that IS the
            ' rule, not a tie-break.
            for each s in path
                for each h in hits
                    if lower(cat.tables[h].schema) = lower(s) then
                        return h
                    end if
                end for
            end for
        end if
        return ""
    end function

    ' discovery.trace(catalog, modules, dbms) ->
    '   { edges, unresolved, gaps }
    '
    ' An edge is { kind: "reads"|"writes", module, object }. `unresolved` and
    ' `gaps` are not diagnostics to be ignored: a tracer that drops what it
    ' could not read produces a lineage graph that is CONFIDENTLY INCOMPLETE,
    ' which is worse than one naming its own holes.
    function trace(cat, mods, dbms)
        edges = []
        unresolved = []
        gaps = []
        for each m in mods
            mid = m.source + "." + m.schema + "." + m.name
            if len(m.schema) = 0 then
                mid = m.source + "." + m.name
            end if
            r = references(m.body)
            path = _search_path(dbms, m.schema)
            for each g in r.gaps
                append(gaps, { module: mid, reason: g })
            end for
            for each side in ["reads", "writes"]
                for each ref in r[side]
                    bound = _bind(cat, ref, m, path)
                    if len(bound) = 0 then
                        append(unresolved, { module: mid, reference: ref,
                                             reason: "no unambiguous object of that name is in the catalog" })
                    else
                        append(edges, { kind: side, module: mid, object: bound })
                    end if
                end for
            end for
        end for
        return { edges: edges, unresolved: unresolved, gaps: gaps }
    end function

    ' discovery.impact(trace, object_id, direction) -> the objects affected.
    '
    ' THE SHORTEST PATH TO VALUE, and what a developer needs BEFORE altering a
    ' column: "downstream" is what consumes this (things that read it, and
    ' whatever they in turn write), "upstream" is what produces it.
    function impact(tr, object_id, direction)
        if not contains(["downstream", "upstream"], direction) then
            error "discovery: direction must be \"downstream\" or \"upstream\""
        end if
        seen = [object_id]
        frontier = [object_id]
        out = []
        hops = 0
        while count(frontier) > 0 and hops < 50
            nxt = []
            for each cur in frontier
                for each e in tr.edges
                    via = ""
                    if direction = "downstream" then
                        ' something READS cur -> whatever that module WRITES
                        if e.kind = "reads" and e.object = cur then
                            via = e.module
                        end if
                    else
                        if e.kind = "writes" and e.object = cur then
                            via = e.module
                        end if
                    end if
                    if len(via) > 0 then
                        if not contains(seen, via) then
                            append(seen, via)
                            append(out, { object: via, kind: "module", hops: hops + 1 })
                        end if
                        other = "writes"
                        if direction = "upstream" then
                            other = "reads"
                        end if
                        for each e2 in tr.edges
                            if e2.module = via and e2.kind = other then
                                if not contains(seen, e2.object) then
                                    append(seen, e2.object)
                                    append(out, { object: e2.object, kind: "table", hops: hops + 1 })
                                    append(nxt, e2.object)
                                end if
                            end if
                        end for
                    end if
                end for
            end for
            frontier = nxt
            hops = hops + 1
        end while
        return out
    end function

    ' ---- column lineage: where THIS number came from -----------------------
    '
    ' `trace` and `impact` answer at the level of OBJECTS: this procedure reads
    ' that table. That is the level a dependency graph is usually drawn at, and
    ' it is one level too coarse for the question people actually ask. "Which
    ' table does this report read" has an easy answer and rarely settles
    ' anything; "where did THIS COLUMN come from" is the one that costs a day.
    '
    ' The difference is not cosmetic. `fact_volume` reads `stg_deal`, and that
    ' says nothing about whether `avail_after_pvr` came from `gross_vol_mmbtu`,
    ' from `pvr_pct`, from both, or from a constant -- and a column that turns
    ' out to be a constant is the single most common reason a number is wrong
    ' and nobody can see why.
    '
    ' WHAT IT WILL NOT DO IS GUESS. A source column that could belong to two
    ' of the objects a statement reads is reported UNRESOLVED, by name, rather
    ' than attributed to whichever was found first: the wrong attribution is
    ' indistinguishable from the right one downstream, and picking one turns a
    ' known unknown into an unknown wrong answer.

    function _owner_of(cid)
        parts = split(cid, _sep())
        out = []
        i = 0
        while i < count(parts) - 1
            append(out, parts[i])
            i = i + 1
        end while
        return join(out, _sep())
    end function

    function _leaf_of(cid)
        parts = split(cid, _sep())
        return parts[count(parts) - 1]
    end function

    ' A CASE-INSENSITIVE INDEX, and it is not a convenience. Catalog names come
    ' back in whatever case the database stores them (`GROSS_VOL_MMBTU` on one,
    ' `gross_vol_mmbtu` on the next) while SQL tokens are lowercased here, so a
    ' direct key lookup misses on exactly the databases that upper-case their
    ' catalog -- silently, reporting the column as unresolved.
    function _column_index(cat)
        idx = {}
        for each k in keys(cat.columns)
            idx[lower(k)] = k
        end for
        return idx
    end function

    ' Every module that can put a value into a given object, WITH the statement
    ' that does it. A view is included as the writer of ITSELF: a view has no
    ' `insert`, and its select list is nonetheless the derivation of every
    ' column a consumer sees, which is the whole reason a report's number can
    ' be traced past the view it was read from.
    function _writers(cat, mods, dbms)
        out = []
        for each m in mods
            mid = m.source + _sep() + m.schema + _sep() + m.name
            if len(m.schema) = 0 then
                mid = m.source + _sep() + m.name
            end if
            path = _search_path(dbms, m.schema)
            self_id = _bind(cat, m.schema + _sep() + m.name, m, path)
            if len(m.schema) = 0 then
                self_id = _bind(cat, m.name, m, path)
            end if
            for each d in derivations(m.body)
                tid = ""
                if len(d.target) > 0 then
                    tid = _bind(cat, d.target, m, path)
                end if
                if len(tid) = 0 and d.kind = "select" and len(self_id) > 0 then
                    ' A view producing its own object.
                    tid = self_id
                end if
                ' A TARGET THAT BINDS TO NOTHING IS STILL A HOP. `select ...
                ' into #tmp` then `insert ... select ... from #tmp` is how a
                ' great deal of real ETL is written, and a temp table is never
                ' in a catalog -- so without this the chain dead-ends at the
                ' first one, which in T-SQL is most procedures. Nothing is
                ' guessed: the statement that FILLS the temp table is what
                ' says which columns it has. The id carries `::` so it can
                ' never be mistaken for a catalog id, and `intermediates`
                ' names every local object a walk passed through.
                local_name = ""
                if len(tid) = 0 and len(d.target) > 0 and count(d.columns) > 0 then
                    local_name = lower(d.target)
                    tid = mid + "::" + local_name
                end if
                ' AN INSERT WITH NO COLUMN LIST IS SETTLED BY THE CATALOG,
                ' and only here: `derivations` is a pure function of the SQL
                ' and cannot know the target's ordinal order, so it reports a
                ' note instead of guessing. This is where that note is
                ' discharged -- by the same rule the database itself applies.
                ' If the counts disagree the statement is not valid SQL and
                ' the note stands.
                if len(tid) > 0 and len(local_name) = 0 then
                    if d.pairing = "ordinal" and count(d.columns) > 0 then
                        tcols = columns_of(cat, tid)
                        if count(tcols) = count(d.columns) then
                            filled = []
                            ci = 0
                            while ci < count(tcols)
                                cc = d.columns[ci]
                                append(filled, { output: tcols[ci].column,
                                                 expression: cc.expression,
                                                 sources: cc.sources,
                                                 source_refs: cc.source_refs,
                                                 aggregate: cc.aggregate })
                                ci = ci + 1
                            end while
                            d.columns = filled
                            d.notes = []
                            d.pairing = "ordinal_settled"
                        end if
                    end if
                end if
                if len(tid) > 0 and (count(d.columns) > 0 or count(d.notes) > 0) then
                    append(out, { table: tid, module: mid, path: path,
                                  schema: m.schema, kind: d.kind,
                                  local_name: local_name, derivation: d })
                end if
            end for
        end for
        return out
    end function

    ' One source reference resolved to a column id, or a reason it was not.
    function _resolve_ref(cat, idx, w, ref, writers)
        cands = []
        objs = w.derivation.aliases.objects
        if len(ref.qualifier) > 0 then
            obj = ref.qualifier
            if has(w.derivation.aliases.alias, ref.qualifier) then
                obj = w.derivation.aliases.alias[ref.qualifier]
            end if
            objs = [obj]
        end if
        for each o in objs
            tid = _bind(cat, o, w, w.path)
            if len(tid) > 0 then
                probe = lower(tid + _sep() + ref.name)
                if has(idx, probe) then
                    if not contains(cands, idx[probe]) then
                        append(cands, idx[probe])
                    end if
                end if
            else
                ' Not in the catalog -- but this module may have filled it
                ' itself, one statement earlier.
                for each wl in writers
                    if len(wl.local_name) > 0 and wl.module = w.module and wl.local_name = lower(o) then
                        for each lc in wl.derivation.columns
                            if lower(lc.output) = lower(ref.name) then
                                probe2 = wl.table + _sep() + lc.output
                                if not contains(cands, probe2) then
                                    append(cands, probe2)
                                end if
                            end if
                        end for
                    end if
                end for
            end if
        end for
        if count(cands) = 1 then
            return { ok: true, column: cands[0], reason: "" }
        end if
        if count(cands) = 0 then
            if len(ref.qualifier) > 0 then
                return { ok: false, column: "",
                         reason: "'" + ref.qualifier + _sep() + ref.name + "': nothing this statement reads is an object of that name with that column" }
            end if
            return { ok: false, column: "",
                     reason: "'" + ref.name + "': none of the objects this statement reads has a column of that name (it may be a variable, a parameter, or a function this parser does not know)" }
        end if
        return { ok: false, column: "",
                 reason: "'" + ref.name + "' is unqualified and " + string(count(cands)) + " of the objects this statement reads have a column of that name; which one it is cannot be decided from the statement" }
    end function

    ' discovery.lineage(catalog, modules, dbms, column_id, options)
    '   -> { steps, origins, unresolved, gaps }
    '
    ' A step is { hops, column, via, kind, expression, sources, predicates }:
    ' the column being explained, the module that filled it, and the columns it
    ' was filled FROM. `origins` are the columns nothing in the estate writes --
    ' where the trail genuinely ends rather than where it was lost.
    '
    ' options: { max_hops }  -- default 20. A cycle is possible and ordinary:
    ' a restatement reads the same table it writes.
    function lineage(cat, mods, dbms, column_id, options)
        max_hops = 20
        if is_record(options) then
            if has(options, "max_hops") then
                max_hops = options.max_hops
            end if
        end if
        idx = _column_index(cat)
        if not has(idx, lower(column_id)) then
            error "discovery: no column '" + column_id + "' is in this catalog; lineage starts from a column that exists"
        end if
        writers = _writers(cat, mods, dbms)

        steps = []
        origins = []
        unresolved = []
        gaps = []
        intermediates = []
        seen = [lower(column_id)]
        frontier = [column_id]
        hops = 0
        while count(frontier) > 0 and hops < max_hops
            nxt = []
            for each cid in frontier
                tid = _owner_of(cid)
                cname = lower(_leaf_of(cid))
                if contains(tid, "::") and not contains(intermediates, tid) then
                    append(intermediates, tid)
                end if
                wrote = false
                for each w in writers
                    if w.table = tid then
                        for each g in w.derivation.gaps
                            append(gaps, { column: cid, module: w.module, reason: g })
                        end for
                        for each n in w.derivation.notes
                            append(unresolved, { column: cid, module: w.module, reason: n })
                        end for
                        for each c in w.derivation.columns
                            if lower(c.output) = cname then
                                wrote = true
                                ups = []
                                for each ref in c.source_refs
                                    r = _resolve_ref(cat, idx, w, ref, writers)
                                    if r.ok then
                                        if not contains(ups, r.column) then
                                            append(ups, r.column)
                                        end if
                                        if not contains(seen, lower(r.column)) then
                                            append(seen, lower(r.column))
                                            append(nxt, r.column)
                                        end if
                                    else
                                        append(unresolved, { column: cid, module: w.module,
                                                             reason: r.reason })
                                    end if
                                end for
                                append(steps, { hops: hops, column: cid, via: w.module,
                                                kind: w.kind, expression: c.expression,
                                                sources: ups,
                                                predicates: w.derivation.predicates })
                            end if
                        end for
                    end if
                end for
                if not wrote then
                    if not contains(origins, cid) then
                        append(origins, cid)
                    end if
                end if
            end for
            frontier = nxt
            hops = hops + 1
        end while
        return { steps: steps, origins: origins, unresolved: unresolved,
                 intermediates: intermediates, gaps: gaps }
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
