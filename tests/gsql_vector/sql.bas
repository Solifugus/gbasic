' A VECTOR column, pgvector's three distances and the functions naming them
' (docs/gsql_vector_design.md). Every answer here is worked by hand from
' vectors chosen so each is exact: [3, 4] is 5 long, [1, 2, 3] . [4, 5, 6] is
' 32. Each refusal sits beside a neighbour that is accepted. The random half,
' judged by Python, is tests/gsql_vector/oracle.bas.
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

' the refusal's code and its words, the statement it names cut off; or "ok"
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

function one(db, q)
    return db.query(q)[0].v
end function

function ids(rows)
    out = []
    for each r in rows
        append(out, r.id)
    next
    return out
end function

db = gsql.connect("vectors.gsql", { create: true })
db.exec("create table doc (id integer primary key, acl text, e vector(3), any_e vector, r record)")

' --- into the column ----------------------------------------------------------
db.exec("insert into doc values (1, 'staff', ?, ?, ?)", [vector([1, 0, 0]), vector([3, 4]), { v: vector([1, 2]) }])
db.exec("insert into doc (id, acl, e) values (2, 'staff', '[0, 1, 0]')")
db.exec("insert into doc (id, acl, e) values (3, 'board', ?)", [[0, 0, 2]])
gsql.insert(db, "doc", [{ id: 4, acl: "staff", e: [1, 1, 0] }, { id: 5, acl: "board", e: vector([2, 0, 0]) }])
got = db.query("select e, any_e, r from doc where id = 1")[0]
check("a vector is stored as itself", got.e, vector([1, 0, 0]))
check("and comes back a vector", type(got.e), "vector")
check("an unsized column holds any dimension", got.any_e, vector([3, 4]))
check("a vector inside a record", got.r.v, vector([1, 2]))
check("text is read as vector() reads it", one(db, "select e as v from doc where id = 2"), vector([0, 1, 0]))
check("an array of numbers is converted", one(db, "select e as v from doc where id = 3"), vector([0, 0, 2]))
check("gsql.insert converts an array too", one(db, "select e as v from doc where id = 4"), vector([1, 1, 0]))
check("a NULL vector", is_unknown(one(db, "select any_e as v from doc where id = 2")), true)
check("the wrong dimension", outcome(db, "insert into doc (id, e) values (9, '[1, 2]')"),
      "TYPE: gsql.exec: doc.e is vector(3), and the vector has 2 dimensions")
check("the right one, beside it", outcome(db, "insert into doc (id, e) values (9, '[1, 2, 3]')"), "ok")
check("a number is no vector", outcome(db, "insert into doc (id, e) values (10, 5)"),
      "TYPE: gsql.exec: doc.e is vector(3), and the value is a number")
check("text that is not a list", outcome(db, "insert into doc (id, e) values (10, 'hello')"),
      "TYPE: gsql.exec: doc.e is vector, and the text given does not read as one: decode error: expected value at byte 0")
check("an element that is not a number", outcome(db, "insert into doc (id, e) values (10, '[1, \"x\", 3]')"),
      "TYPE: gsql.exec: doc.e is vector, and the text given does not read as one: a vector: the element at index 1 is a string, not a number")
check("an empty list", outcome(db, "insert into doc (id, e) values (10, '[]')"),
      "TYPE: gsql.exec: doc.e is vector, and the text given does not read as one: a vector: an empty array makes no vector; a vector has at least one number")
check("past float32", outcome(db, "insert into doc (id, e) values (10, '[1e39, 0, 0]')"),
      "TYPE: gsql.exec: doc.e is vector, and the text given does not read as one: a vector: the element at index 0 is 1e+39, and a vector holds finite numbers a float32 can (up to about 3.4e38)")
db.exec("delete from doc where id = 9")

' --- the three operators, worked by hand --------------------------------------
' [3, 4, 0] against [0, 0, 0] is 5 apart; [1, 2, 3] . [4, 5, 6] = 4 + 10 + 18 = 32;
' [1, 0, 0] and [0, 1, 0] are at right angles, cosine 0, so distance 1;
' [1, 0, 0] and [2, 0, 0] point one way, distance 0; [1, 0] and [-1, 0] opposite, 2
check("<-> is Euclidean", one(db, "select '[3, 4, 0]' <-> cast('[0, 0, 0]' as vector) as v"), 5)
check("<#> is the negated product", one(db, "select cast('[1, 2, 3]' as vector) <#> '[4, 5, 6]' as v"), -32)
check("<=> at right angles", one(db, "select e <=> '[0, 1, 0]' as v from doc where id = 1"), 1)
check("<=> one direction", one(db, "select e <=> '[2, 0, 0]' as v from doc where id = 1"), 0)
check("<=> opposite", one(db, "select cast('[1, 0]' as vector) <=> '[-1, 0]' as v"), 2)
check("a parameter vector", db.query("select e <-> ? as v from doc where id = 5", [vector([2, 3, 0])])[0].v, 3)
check("an array parameter", db.query("select e <-> ? as v from doc where id = 5", [[2, 0, 4]])[0].v, 4)
check("NULL in, NULL out", is_unknown(one(db, "select any_e <-> '[1, 1]' as v from doc where id = 2")), true)
check("NULL on the right", is_unknown(one(db, "select e <=> null as v from doc where id = 1")), true)

' --- precedence: PostgreSQL's level, looser than + and -, tighter than < -------
' e + e of row 1 is [2, 0, 0]; its distance from [5, 4, 0] is 5
check("+ binds before <->", one(db, "select '[5, 4, 0]' <-> e + e as v from doc where id = 1"), 5)
check("<-> binds before <", one(db, "select e <-> '[4, 4, 0]' < 6 as v from doc where id = 1"), true)
check("and before =", one(db, "select e <-> '[4, 4, 0]' = 5 as v from doc where id = 1"), true)
check("left to right: a distance is no vector", outcome(db, "select '[1, 0, 0]' <-> e <-> e as v from doc where id = 1"),
      "EXPRESSION: gsql.query: <-> measures between two vectors, and one side is a number")

' --- the functions --------------------------------------------------------------
check("l2_distance", one(db, "select l2_distance(cast('[3, 4]' as vector), '[0, 0]') as v"), 5)
check("cosine_distance", one(db, "select cosine_distance(e, '[0, 3, 0]') as v from doc where id = 1"), 1)
check("inner_product is the product itself", one(db, "select inner_product(cast('[1, 2, 3]' as vector), '[4, 5, 6]') as v"), 32)
check("negative_inner_product is <#>", one(db, "select negative_inner_product(cast('[1, 2, 3]' as vector), '[4, 5, 6]') as v"), -32)
check("gBASIC's distance, l2 by default", one(db, "select distance(cast('[3, 4]' as vector), '[0, 0]') as v"), 5)
check("distance with a metric", one(db, "select distance(e, '[0, 0, 7]', 'cosine') as v from doc where id = 1"), 1)
check("distance ip", one(db, "select distance(cast('[1, 2, 3]' as vector), '[4, 5, 6]', 'ip') as v"), -32)
check("vector_dims", one(db, "select vector_dims(any_e) as v from doc where id = 1"), 2)
check("vector_norm", one(db, "select vector_norm(any_e) as v from doc where id = 1"), 5)
check("+ and * are gBASIC's", one(db, "select e * 3 + e as v from doc where id = 4"), vector([4, 4, 0]))
check("a function's NULL", is_unknown(one(db, "select vector_norm(any_e) as v from doc where id = 2")), true)

' --- search: the filter and the ranking in one statement ------------------------
' from [1, 0, 0]: rows 1 and 5 point its way (0), 4 is 45 degrees off, 2 and 3 at right angles;
' from [2, 0, 0] by l2: 5 is 0, 1 is 1, 4 is sqrt 2; against [1, 1, 0] the products are
' 1, 1, 0, 2, 2, so 4 and 5 tie nearest and come in id order; from [1, 0, 0] row 1 is 0,
' rows 4 and 5 are 1, row 2 sqrt 2 (past 1.1)
check("nearest by cosine, ties in id order", ids(db.query("select id from doc order by e <=> '[1, 0, 0]', id limit 3")), [1, 5, 4])
check("the filter narrows before the ranking", ids(db.query("select id from doc where acl = 'board' order by e <=> ? limit 2", [vector([1, 0, 0])])), [5, 3])
check("by l2", ids(db.query("select id from doc order by e <-> '[2, 0, 0]' limit 2")), [5, 1])
check("by product, largest first", ids(db.query("select id from doc order by e <#> '[1, 1, 0]' limit 2")), [4, 5])
check("descending, the farthest", ids(db.query("select id from doc order by e <-> '[1, 0, 0]' desc limit 1")), [3])
check("a distance in WHERE", ids(db.query("select id from doc where e <-> '[1, 0, 0]' < 1.1 order by id")), [1, 4, 5])
check("a distance as a column", db.query("select id, e <-> '[1, 0, 0]' as d from doc where id = 5")[0].d, 1)

' --- comparison, CAST and text ------------------------------------------------
check("= compares element by element", one(db, "select e = '[1, 0, 0]' as v from doc where id = 1"), true)
check("<> too", one(db, "select e <> '[1, 0, 0]' as v from doc where id = 2"), true)
check("CAST to TEXT is pgvector's text", one(db, "select cast(e as text) as v from doc where id = 4"), "[1,1,0]")
check("a fraction prints its shortest float32", one(db, "select cast(cast('[0.1, 0.25]' as vector) as text) as v"), "[0.1,0.25]")
check("CAST(text AS VECTOR(3))", one(db, "select cast('[1, 2, 3]' as vector(3)) as v"), vector([1, 2, 3]))
check("CAST to the wrong dimension", outcome(db, "select cast('[1, 2]' as vector(3)) as v"),
      "EXPRESSION: gsql.query: CAST to VECTOR(3): the vector has 2 dimensions")
check("an unsized CAST takes any", one(db, "select cast('[1, 2]' as vector) as v"), vector([1, 2]))
cols = gsql.columns(db, "select e, any_e from doc")
check("columns says vector(3)", cols[0].kind, "vector(3)")
check("and an unsized one vector", cols[1].kind, "vector")

' --- refusals, each beside the form that works ---------------------------------
check("no index over a vector", outcome(db, "create index ie on doc (e)"),
      "SCHEMA: gsql.exec: doc.e is a vector, which index ie cannot hold: a vector has no order; it is searched by distance, as in ORDER BY e <=> ? LIMIT 10")
check("an index beside it", outcome(db, "create index ia on doc (acl)"), "ok")
check("no key", outcome(db, "create table k (e vector primary key)"),
      "SCHEMA: gsql.exec: k.e is a vector, so it cannot be in a key: a vector has no order; it is searched by distance, as in ORDER BY e <=> ? LIMIT 10")
check("nor unique", outcome(db, "create table k (id integer primary key, e vector unique)"),
      "SCHEMA: gsql.exec: k.e is a vector, which index k_e_key cannot hold: a vector has no order; it is searched by distance, as in ORDER BY e <=> ? LIMIT 10")
check("ORDER BY the vector itself", outcome(db, "select id from doc order by e"),
      "EXPRESSION: gsql.query: a vector has no order; sort by its distance to another, as in ORDER BY e <=> ? LIMIT 10")
check("< between vectors", outcome(db, "select e < e as v from doc"),
      "EXPRESSION: gsql.query: vectors support only = and !=; compare their distances instead")
check("GROUP BY one", outcome(db, "select e from doc group by e"),
      "EXPRESSION: gsql.query: a vector cannot be grouped or made DISTINCT: G-SQL has no key for it")
check("DISTINCT", outcome(db, "select distinct e from doc"),
      "EXPRESSION: gsql.query: a vector cannot be grouped or made DISTINCT: G-SQL has no key for it")
check("a number beside <->", outcome(db, "select e <-> 5 as v from doc"),
      "EXPRESSION: gsql.query: <-> measures between two vectors, and one side is a number")
check("a boolean beside a function", outcome(db, "select inner_product(e, true) as v from doc"),
      "EXPRESSION: gsql.query: inner_product measures between two vectors, and one side is a boolean")
check("two dimensions", outcome(db, "select e <-> any_e as v from doc where id = 1"),
      "EXPRESSION: gsql.query: distance: these vectors have 3 and 2 dimensions, and only vectors of one dimension compare")
check("a zero vector has no cosine", outcome(db, "select e <=> '[0, 0, 0]' as v from doc"),
      "EXPRESSION: gsql.query: distance: the second vector is all zeros, which has no direction, so no cosine")
check("but it has a length", outcome(db, "select e <-> '[0, 0, 0]' as v from doc"), "ok")
check("an unknown metric", outcome(db, "select distance(e, e, 'manhattan') as v from doc"),
      "EXPRESSION: gsql.query: distance: the metric is \"l2\", \"cosine\" or \"ip\", not \"manhattan\"")
check("a metric that is not text", outcome(db, "select distance(e, e, 2) as v from doc"),
      "EXPRESSION: gsql.query: distance's metric is text: 'l2', 'cosine' or 'ip'")
check("too many arguments", outcome(db, "select l2_distance(e, e, e) as v from doc"),
      "SYNTAX: gsql.query: l2_distance takes 2 arguments, and was given 3")
check("a dimension of 0", outcome(db, "create table z (e vector(0))"),
      "SYNTAX: gsql.exec: a vector has 1 to 16000 dimensions")
check("past pgvector's limit", outcome(db, "create table z (e vector(16001))"),
      "SYNTAX: gsql.exec: a vector has 1 to 16000 dimensions")
check("pgvector's limit itself", outcome(db, "create table z (e vector(16000))"), "ok")
check("a dimension that is not a number", outcome(db, "create table y (e vector(n))"),
      "SYNTAX: gsql.exec: expected the vector's dimension, a whole number, as in VECTOR(1536), found `n`")
check("no currency on a vector", outcome(db, "create table y (e vector(usd))"),
      "SYNTAX: gsql.exec: expected the vector's dimension, a whole number, as in VECTOR(1536), found `usd`")

' --- a shape held to the table -------------------------------------------------
shape held
    id:number
    e:vector(3)
end shape
shape wrong
    id:number
    e:vector(4)
end shape
shape unsized
    any_e:vector
end shape
on error goto next
gsql.expect(db, "doc", held)
check("expect holds vector(3) to vector(3)", error, false)
gsql.expect(db, "doc", unsized)
check("an unsized lock to an unsized column", error, false)
gsql.expect(db, "doc", wrong)
check("expect refuses vector(4)", error.message,
      "gsql.expect: table doc does not hold shape wrong (wrong is declared at line 193): wrong.e is vector(4), and column e is vector(3)")
on error stop
rows = db.query("select id, e from doc where id = 4")
r:held = rows[0]
check("a row read into the shape", r.e, vector([1, 1, 0]))

' --- a dump and restore puts back the same bytes --------------------------------
db.exec("insert into doc (id, acl, e, any_e) values (20, 'x', '[0.1, -0.2, 3.5e-7]', '[1e30]')")
gsql.dump(db, "vectors.sql")
dumped = read("vectors.sql")
check("the dump writes VECTOR(3)", not is_unknown(find(dumped, "VECTOR(3)")), true)
check("and a value as CAST(text AS VECTOR)", not is_unknown(find(dumped, "CAST('[0.1,-0.2,3.5e-7]' AS VECTOR)")), true)
db2 = gsql.connect("restored.gsql", { create: true })
gsql.restore(db2, "vectors.sql")
before = db.query("select id, e, any_e from doc order by id")
after = db2.query("select id, e, any_e from doc order by id")
mism = 0
for each b, i in before
    if serialize(b) != serialize(after[i]) then
        mism = mism + 1
    end if
next
check("as many rows", len(after), len(before))
check("every row the same serialize bytes", mism, 0)
check("the restored column still refuses a wrong dimension", outcome(db2, "insert into doc (id, e) values (99, '[1]')"),
      "TYPE: gsql.exec: doc.e is vector(3), and the vector has 1 dimension")

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
