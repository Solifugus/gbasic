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
    function references(sql)
        toks = _sql_tokens(sql)
        reads = []
        writes = []
        gaps = []
        ctes = []

        ' A CTE name is `word as (`. A table alias is `word as word`, and a
        ' derived table is `) as word`, so the shape is specific enough.
        i = 0
        while i + 2 < count(toks)
            if toks[i].kind = "word" and toks[i + 1].kind = "word" and toks[i + 1].text = "as" then
                if toks[i + 2].kind = "punct" and toks[i + 2].text = "(" then
                    append(ctes, toks[i].text)
                end if
            end if
            i = i + 1
        end while

        prev = ""
        i = 0
        while i < count(toks)
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
                if w = "update" then
                    target = "write"
                end if
                if w = "table" and (prev = "truncate" or prev = "drop" or prev = "alter") then
                    target = "write"
                end if
                if w = "sp_executesql" or w = "immediate" then
                    append(gaps, "dynamic SQL: " + w)
                end if
                if (w = "exec" or w = "execute") and i + 1 < count(toks) then
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
            append(out, txt)
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
    function projection(sql)
        toks = _sql_tokens(sql)
        out = []
        ' Find the first top-level SELECT and the FROM that closes its list.
        depth = 0
        start = -1
        list_end = -1
        i = 0
        while i < count(toks)
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
            list_end = count(toks)
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
                while j < i
                    if _kw_at(toks, j, "as") then
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
                              aggregate: agg })
                item_start = i + 1
            end if
            i = i + 1
        end while
        return out
    end function

    ' discovery.predicates(sql) -> array of { kind, text }
    '
    ' `where` and `having` narrow which rows contribute; a join `on` decides
    ' which rows pair up and can narrow just as effectively. All three belong
    ' to the derivation, because all three are answers to "why is this number
    ' different from that one".
    function predicates(sql)
        toks = _sql_tokens(sql)
        out = []
        i = 0
        while i < count(toks)
            tk = toks[i]
            if tk.kind = "word" and contains(["where", "having", "on"], tk.text) then
                kind = tk.text
                if kind = "on" then
                    kind = "join"
                end if
                j = i + 1
                depth = 0
                while j < count(toks)
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
                        if contains(["where", "group", "order", "having", "join", "union", "select", "inner", "left", "right", "full", "cross"], t2.text) then
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
