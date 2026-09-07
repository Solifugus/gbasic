' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' retrieval -- permission-filtered nearest-neighbour search over pgvector.
'
' docs/gbasic_ai_reference_and_primitives.md §1.7, step 7.
'
' THE WHOLE POINT IS THAT THE PERMISSION FILTER AND THE RANKING ARE ONE QUERY.
' The obvious implementation ranks first and then drops what the caller may not
' see, and it is wrong in a way that looks like an ordinary empty result: a user
' with narrow permissions asks a question, the global nearest chunks all belong
' to someone else, every one is dropped, and they are told nothing matched.
' Nothing errors. The right answer -- their OWN nearest chunks -- was never
' considered.
'
' Putting the predicate in the WHERE clause makes Postgres filter before it
' orders, so the caller gets their own top-k. That is the entire library, and
' `tests/run_retrieval.sh` asserts it as a difference between a user who may see
' the nearest chunks and one who may not.
'
' The ACL is a `text[]` and the predicate is `acl && $2` -- the two-character
' overlap operator -- which needs `pg` to send a native array. A jsonb join does
' the same job and reads the same way; the array is simply shorter now that the
' module can send one.
library retrieval

    load pg

    ' An identifier cannot be bound, so it is VALIDATED and refused rather than
    ' escaped -- the rule `dbframe` follows for the same reason. Escaping an
    ' identifier is a decision about a quoting dialect; refusing one is a fact.
    function _table(name)
        if not is_string(name) then
            error "retrieval: the table name must be a string"
        end if
        if len(name) = 0 or len(name) > 63 then
            error "retrieval: '" + string(name) + "' is not a usable table name"
        end if
        i = 0
        while i < len(name)
            c = mid(name, i, 1)
            ok = (c >= "a" and c <= "z") or (c >= "0" and c <= "9") or c = "_"
            if not ok then
                error "retrieval: table name '" + name + "' may use only lowercase letters, digits and underscore -- an identifier cannot be a bound parameter, so it is refused rather than escaped"
            end if
            i = i + 1
        end while
        first = mid(name, 0, 1)
        if first >= "0" and first <= "9" then
            error "retrieval: table name '" + name + "' may not start with a digit"
        end if
        return name
    end function

    function _vector_text(v)
        if not is_array(v) then
            error "retrieval: a vector must be an array of numbers"
        end if
        if count(v) = 0 then
            error "retrieval: a vector must not be empty"
        end if
        parts = []
        for each n in v
            if not is_number(n) then
                error "retrieval: a vector must be an array of numbers"
            end if
            append(parts, string(n))
        end for
        return "[" + join(parts, ",") + "]"
    end function

    ' The DDL, as statements to run. Returned rather than executed so a caller
    ' can see what it is about to do to their database -- and so a migration
    ' tool can own the running.
    function schema(table, dimensions)
        t = _table(table)
        if not is_number(dimensions) or dimensions < 1 then
            error "retrieval: the vector dimension must be a positive number"
        end if
        return [
            "create extension if not exists vector",
            "create table if not exists " + t + " (" +
                "id text primary key, " +
                "source text not null, " +
                "text text not null, " +
                "hash text not null, " +
                "acl text[] not null, " +
                "vec vector(" + string(dimensions) + ") not null)",
            "create index if not exists " + t + "_acl on " + t + " using gin (acl)"
        ]
    end function

    function create(db, table, dimensions)
        for each stmt in schema(table, dimensions)
            pg.exec(db, stmt, [])
        end for
        return nothing
    end function

    function _check_chunk(c)
        if not is_record(c) then
            error "retrieval: every chunk must be a record"
        end if
        for each f in ["id", "source", "text", "hash", "acl", "vec"]
            if not has(c, f) then
                error "retrieval: a chunk has no '" + f + "'"
            end if
        end for
        if not is_string(c.id) then
            error "retrieval: a chunk id must be a string"
        end if
        if not is_array(c.acl) then
            error "retrieval: a chunk's acl must be an array of group names (use [] for nobody)"
        end if
        for each g in c.acl
            if not is_string(g) then
                error "retrieval: a chunk's acl must contain only strings"
            end if
        end for
        return nothing
    end function

    ' Upsert, KEYED BY CONTENT HASH: a chunk whose hash has not changed is left
    ' alone, which is what makes an indexer resumable -- a crash mid-run costs
    ' the batch in flight and nothing else.
    function store(db, table, chunks)
        t = _table(table)
        if not is_array(chunks) then
            error "retrieval: store expects an array of chunks"
        end if
        written = 0
        skipped = 0
        for each c in chunks
            _check_chunk(c)
            ' Parenthesised because gBASIC continues a statement only inside an
            ' unclosed bracket (PLAT-CONT); a trailing `+` does not continue it.
            sql = ("insert into " + t + " (id, source, text, hash, acl, vec) " +
                   "values ($1, $2, $3, $4, $5, $6::vector) " +
                   "on conflict (id) do update set " +
                   "source = excluded.source, text = excluded.text, " +
                   "hash = excluded.hash, acl = excluded.acl, vec = excluded.vec " +
                   "where " + t + ".hash is distinct from excluded.hash")
            r = pg.exec(db, sql, [ c.id, c.source, c.text, c.hash, c.acl,
                                   _vector_text(c.vec) ])
            ' `pg.exec` answers { command, rows_affected }, and rows_affected is
            ' `nothing` for a statement that affects no rows. Written as
            ' `if r > 0` first, which PLAT-EQ's ordering refusal caught -- before
            ' that fix a record compared as 0 and every write would have been
            ' counted as skipped, silently.
            n = 0
            if is_number(r.rows_affected) then
                n = r.rows_affected
            end if
            if n > 0 then
                written = written + 1
            else
                skipped = skipped + 1
            end if
        end for
        return { written: written, skipped: skipped }
    end function

    ' THE QUERY. The permission predicate is in the WHERE and the distance
    ' ordering follows it, so Postgres narrows to what this caller may see and
    ' THEN finds the nearest among those. Ranking first and filtering after
    ' returns an empty list to a narrowly permitted user whose own best matches
    ' were never looked at -- and an empty list reads as "nothing matched".
    function search(db, table, qvec, groups, limit)
        t = _table(table)
        if not is_array(groups) then
            error "retrieval: search expects the caller's groups as an array"
        end if
        if not is_number(limit) or limit < 1 then
            error "retrieval: search needs a positive limit"
        end if
        ' THE SAME TEXT `query_text` RETURNS, not a second copy of it. Written
        ' as two separate string builds first, and the offline tier then passed
        ' on a perturbation that changed only `search` -- two representations of
        ' one query, exactly the drift a tripwire guards elsewhere in this tree.
        ' One source removes the need for the tripwire.
        return pg.query(db, query_text(t), [ _vector_text(qvec), groups, limit ])
    end function

    ' What `search` would run, for a caller that wants to see or explain it.
    ' Exposed because a permission predicate nobody can read is a permission
    ' predicate nobody audits.
    function query_text(table)
        t = _table(table)
        return ("select id, source, text, vec <-> $1::vector as distance from " +
                t + " where acl && $2 order by distance limit $3")
    end function

end library
