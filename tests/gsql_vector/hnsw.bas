' G-SQL's HNSW index from a program (docs/gsql_vector_design.md §9): its
' declaration and refusals, when the planner takes it (EXPLAIN), and a
' differential against the exact search. The same rows are in `idx`, which has
' the index, and in `plain`, which has none; at 30 rows the graph holds every
' node in its beam, so every query must answer exactly as the exact search
' does, ids and distances, through filters, OFFSET, NULL vectors, ties, and
' the writes that keep the graph. Recall at scale is the runner's recall tier.
' Run by tests/run_gsql_vector.sh from inside an empty directory.
load gsql

checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

function outcome(db, q)
    on error goto next
    if starts_with(q, "select") then
        db.query(q)
    else
        db.exec(q)
    end if
    if error then
        m = error.message
        cut = find(m, " [")
        if not (is_unknown(cut) or cut = nothing) then
            m = mid(m, 0, cut)
        end if
        return error.details.code + ": " + m
    end if
    return "ok"
end function

' the access path EXPLAIN names first
function path_of(db, q)
    return db.query("explain " + q)[0].plan
end function

' a query against both tables, answers compared whole
function same(db, label, q, params)
    a = db.query(replace(q, "TBL", "idx"), params)
    b = db.query(replace(q, "TBL", "plain"), params)
    check(label, a, b)
    return nothing
end function

db = gsql.connect("hnsw.gsql", { create: true })
db.exec("create table idx (id integer primary key, tag text, e vector(3))")
db.exec("create table plain (id integer primary key, tag text, e vector(3))")
db.exec("create index idx_cos on idx using hnsw (e vector_cosine_ops) with (m = 4, ef_construction = 8)")
db.exec("create index idx_l2 on idx using hnsw (e)")
db.exec("create index idx_tag on idx using btree (tag)")

' --- the rows: copies make ties, some have no vector ---------------------------
rows = []
for i = 1 to 30
    tag = ["red", "green", "blue"][mod(i, 3)]
    if mod(i, 7) = 0 then
        e = unknown
    else
        e = vector([mod(i * 5, 11) - 5 + 0.5, mod(i * 3, 7) - 3, mod(i, 4) + 1])
    end if
    if mod(i, 10) = 4 then
        e = vector([1.5, 0, 2])   ' the same vector three times
    end if
    append(rows, { id: i, tag: tag, e: e })
next
gsql.insert(db, "idx", rows)
gsql.insert(db, "plain", rows)

' --- when the planner takes the graph -------------------------------------------
q1 = "select id from idx order by e <=> '[1, 0, 1]' limit 5"
check("<=> with LIMIT", path_of(db, q1), "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
check("<-> takes the l2 graph", path_of(db, "select id from idx order by e <-> '[1, 0, 1]' limit 5"),
      "HNSW Search on idx using idx_l2 (nearest '[1, 0, 1]')")
check("the function's name", path_of(db, "select id from idx order by cosine_distance(e, '[1, 0, 1]') limit 5"),
      "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
check("the query on the left", path_of(db, "select id from idx order by '[1, 0, 1]' <=> e limit 5"),
      "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
check("by its alias", path_of(db, "select id, e <=> '[1, 0, 1]' as d from idx order by d limit 5"),
      "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
check("a parameter", path_of(db, "select id from idx order by e <=> ? limit 5"), "HNSW Search on idx using idx_cos (nearest $1)")
check("with a filter", path_of(db, "select id from idx where tag = 'red' order by e <=> '[1, 0, 1]' limit 5"),
      "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
check("no graph for <#>", path_of(db, "select id from idx order by e <#> '[1, 0, 1]' limit 5"), "Scan on idx")
check("nor DESC", path_of(db, "select id from idx order by e <=> '[1, 0, 1]' desc limit 5"), "Scan on idx")
check("nor without LIMIT", path_of(db, "select id from idx order by e <=> '[1, 0, 1]'"), "Scan on idx")
check("nor ordered first by another", path_of(db, "select id from idx order by tag, e <=> '[1, 0, 1]' limit 5"),
      "Scan on idx")
check("nor grouped", path_of(db, "select tag from idx group by tag order by min(e <=> '[1, 0, 1]') limit 2"),
      "Scan on idx")
check("nor a query vector read from a column", path_of(db, "select id from idx order by e <=> e limit 5"), "Scan on idx")
check("nor a join", starts_with(path_of(db, "select idx.id from idx join plain on idx.id = plain.id order by idx.e <=> '[1, 0, 1]' limit 5"), "HNSW"),
      false)

' --- the differential: the graph answers as the exact search ---------------------
for each m in ["<=>", "<->"]
    for each k in [1, 3, 5, 10, 26, 40]
        same(db, m + " limit " + string(k), "select id, e " + m + " '[1, 0.5, 2]' as d from TBL order by e " + m + " '[1, 0.5, 2]' limit " + string(k), [])
        same(db, m + " red, limit " + string(k), "select id, e " + m + " '[1, 0.5, 2]' as d from TBL where tag = 'red' order by e " + m + " '[1, 0.5, 2]' limit " + string(k), [])
    next
    same(db, m + " offset", "select id from TBL order by e " + m + " '[-2, 1, 1]' limit 4 offset 3", [])
    same(db, m + " offset past the vectors", "select id from TBL order by e " + m + " '[-2, 1, 1]' limit 10 offset 22", [])
    same(db, m + " a parameter", "select id from TBL order by e " + m + " ? limit 6", [vector([0.5, -1, 3])])
    same(db, m + " an array parameter", "select id from TBL order by e " + m + " ? limit 6", [[0.5, -1, 3]])
    same(db, m + " a filter no row passes", "select id from TBL where tag = 'none' order by e " + m + " '[1, 1, 1]' limit 3", [])
    same(db, m + " a second key", "select id from TBL order by e " + m + " '[1.5, 0, 2]', id desc limit 5", [])
next
' the three copies tie at distance 0, in id order as the exact sort keeps them
check("ties come in id order", db.query("select id from idx order by e <=> '[1.5, 0, 2]' limit 3"),
      [{ id: 4 }, { id: 14 }, { id: 24 }])
' 26 rows have a vector: a LIMIT past them ends with the rest, as NULL sorts last
check("the rows with no vector come last", len(db.query("select id from idx order by e <-> '[0, 0, 0]' limit 30")), 30)
same(db, "every row, the NULLs last", "select id from TBL order by e <-> '[0, 0, 0]' limit 30", [])

' --- a query the graph cannot answer is the exact search's, in its words ----------
check("another dimension", outcome(db, "select id from idx order by e <=> '[1, 0]' limit 3"),
      "EXPRESSION: gsql.query: distance: these vectors have 3 and 2 dimensions, and only vectors of one dimension compare")
check("a zero query under cosine", outcome(db, "select id from idx order by e <=> '[0, 0, 0]' limit 3"),
      "EXPRESSION: gsql.query: distance: the second vector is all zeros, which has no direction, so no cosine")
same(db, "a zero query under l2", "select id from TBL order by e <-> '[0, 0, 0]' limit 3", [])
same(db, "a NULL query", "select id from TBL order by e <-> null limit 3", [])
check("text that is no vector", outcome(db, "select id from idx order by e <-> 'hello' limit 3"),
      "EXPRESSION: gsql.query: decode error: expected value at byte 0")

' --- the writes that keep the graph ------------------------------------------------
for each t in ["idx", "plain"]
    db.exec("update " + t + " set e = '[2, 2, 2]' where id = 5")
    db.exec("update " + t + " set e = null where id = 8")
    db.exec("update " + t + " set e = '[-1, 3, 1]' where id = 7")
    db.exec("update " + t + " set id = 105 where id = 11")
    db.exec("delete from " + t + " where id in (1, 2, 14)")
    db.exec("insert into " + t + " values (40, 'red', '[2, 2, 2]'), (41, 'blue', null)")
next
for each m in ["<=>", "<->"]
    same(db, m + " after the writes", "select id, e " + m + " '[2, 1.9, 2]' as d from TBL order by e " + m + " '[2, 1.9, 2]' limit 40", [])
    same(db, m + " after the writes, filtered", "select id from TBL where tag = 'blue' order by e " + m + " '[2, 1.9, 2]' limit 4", [])
next
check("a moved row is found under its new key", db.query("select id from idx where id > 100 order by e <-> '[0, 0, 0]' limit 1"),
      [{ id: 105 }])

' --- the declaration's refusals, each beside the form accepted --------------------
check("a zero vector into a cosine graph", outcome(db, "insert into idx values (50, 'red', '[0, 0, 0]')"),
      "TYPE: gsql.exec: idx.e: the vector is all zeros, which has no direction, so cosine index idx_cos cannot hold it")
db.exec("create table z (id integer primary key, e vector(2))")
db.exec("create index z_l2 on z using hnsw (e vector_l2_ops)")
check("an l2 graph holds one", outcome(db, "insert into z values (1, '[0, 0]')"), "ok")
check("nor unique", outcome(db, "create unique index u on z using hnsw (e)"),
      "SCHEMA: gsql.exec: hnsw index u cannot be unique: it finds what is near, not what is equal")
check("a vector of any dimension", outcome(db, "create table w (id integer primary key, e vector, t text)") + " / " +
      outcome(db, "create index w_e on w using hnsw (e)"),
      "ok / SCHEMA: gsql.exec: hnsw index w_e needs a vector column of one dimension, as in VECTOR(1536), and w.e is a vector of any dimension")
check("a text column", outcome(db, "create index w_t on w using hnsw (t)"),
      "SCHEMA: gsql.exec: hnsw index w_t needs a vector column of one dimension, as in VECTOR(1536), and w.t is string")
check("two columns", outcome(db, "create index z2 on z using hnsw (e, id)"),
      "UNSUPPORTED: gsql.exec: an hnsw index is over one vector column")
check("m of 1", outcome(db, "create index z3 on z using hnsw (e) with (m = 1)"),
      "SCHEMA: gsql.exec: hnsw index z3: m is 2 to 100, not 1")
check("m of 2", outcome(db, "create index z3 on z using hnsw (e) with (m = 2, ef_construction = 4)"), "ok")
check("m past 100", outcome(db, "create index z4 on z using hnsw (e) with (m = 101)"),
      "SCHEMA: gsql.exec: hnsw index z4: m is 2 to 100, not 101")
check("ef_construction under twice m", outcome(db, "create index z5 on z using hnsw (e) with (m = 8, ef_construction = 15)"),
      "SCHEMA: gsql.exec: hnsw index z5: ef_construction is 4 to 1000 and at least twice m (16), not 15")
check("twice m exactly", outcome(db, "create index z5 on z using hnsw (e) with (m = 8, ef_construction = 16)"), "ok")
check("an operator class G-SQL lacks", outcome(db, "create index z6 on z using hnsw (e vector_l1_ops)"),
      "UNSUPPORTED: gsql.exec: `vector_l1_ops` is not an operator class of G-SQL's hnsw: write vector_l2_ops (<->), vector_cosine_ops (<=>) or vector_ip_ops (<#>)")
check("ivfflat", outcome(db, "create index z7 on z using ivfflat (e)"),
      "UNSUPPORTED: gsql.exec: `ivfflat` is not an index method of G-SQL: the methods are btree (the default) and hnsw, for a vector column; hnsw needs no training step")
check("a parameter hnsw lacks", outcome(db, "create index z8 on z using hnsw (e) with (lists = 100)"),
      "UNSUPPORTED: gsql.exec: an hnsw index takes m and ef_construction, not `lists`")
check("m twice", outcome(db, "create index z9 on z using hnsw (e) with (m = 4, m = 5)"),
      "SYNTAX: gsql.exec: m is given twice")
check("a plain index over a vector, still refused", outcome(db, "create index z10 on z (e)"),
      "SCHEMA: gsql.exec: z.e is a vector, which index z10 cannot hold: a vector has no order; it is searched by distance, as in ORDER BY e <=> ? LIMIT 10")

' --- ef_search, the connection's first beam ------------------------------------------
check("ef_search's default", db.ef_search, 40)
db.ef_search = 2
check("set", db.ef_search, 2)
same(db, "a narrow beam still answers whole", "select id, e <=> '[1, 0.5, 2]' as d from TBL order by e <=> '[1, 0.5, 2]' limit 12", [])
db.ef_search = unknown
check("unknown is the default again", db.ef_search, 40)
on error goto next
db.ef_search = 1001
e = error
check("past 1000", e.message, "ef_search takes a whole number from 1 to 1000, as pgvector's hnsw.ef_search does (or unknown, for the default of 40), not that number")
db.ef_search = "wide"
e = error
check("text", e.message, "ef_search takes a whole number from 1 to 1000, as pgvector's hnsw.ef_search does (or unknown, for the default of 40), not a string")
on error stop

' --- a narrow filter widens the beam until it has the rows ---------------------------
' 300 rows, 6 of them rare: from a beam of 5 the search must double and double
' again (never answering short), and then answer as the exact search does
db.exec("create table bidx (id integer primary key, tag text, e vector(3))")
db.exec("create table bplain (id integer primary key, tag text, e vector(3))")
db.exec("create index bidx_e on bidx using hnsw (e vector_l2_ops) with (m = 4, ef_construction = 8)")
big = []
for i = 1 to 300
    append(big, { id: i, tag: "rare" if mod(i, 50) = 7 else "common", e: vector([mod(i * 7, 23) - 11, mod(i * 5, 17) - 8, mod(i, 13) - 6]) })
next
gsql.insert(db, "bidx", big)
gsql.insert(db, "bplain", big)
db.ef_search = 2
for each k in [1, 3, 6, 9]
    a = db.query("select id, e <-> '[1, 1, 1]' as d from bidx where tag = 'rare' order by e <-> '[1, 1, 1]' limit " + string(k))
    b = db.query("select id, e <-> '[1, 1, 1]' as d from bplain where tag = 'rare' order by e <-> '[1, 1, 1]' limit " + string(k))
    check("a narrow filter, limit " + string(k) + ", widened to the rows", a, b)
next
db.ef_search = unknown

' --- a dump and restore builds the graph again, answering the same ----------------------
gsql.dump(db, "hnsw.sql")
dumped = read("hnsw.sql")
check("the dump declares the graph", not is_unknown(find(dumped, "CREATE INDEX idx_cos ON idx USING hnsw (e vector_cosine_ops) WITH (m = 4, ef_construction = 8);")), true)
check("and the default one", not is_unknown(find(dumped, "CREATE INDEX idx_l2 ON idx USING hnsw (e vector_l2_ops) WITH (m = 16, ef_construction = 64);")), true)
db2 = gsql.connect("restored.gsql", { create: true })
gsql.restore(db2, "hnsw.sql")
check("the restored one searches by its graph", path_of(db2, q1), "HNSW Search on idx using idx_cos (nearest '[1, 0, 1]')")
for each m in ["<=>", "<->"]
    q = "select id, e " + m + " '[0.5, 1, 1]' as d from idx order by e " + m + " '[0.5, 1, 1]' limit 40"
    check(m + " restored, the same answers", db2.query(q), db.query(q))
next

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
