' Native Postgres arrays through `pg`, in both directions.
'
' SELF-CHECKING, and the load-bearing tier uses POSTGRES AS THE ORACLE: an
' array parameter is rendered as a literal and read back through
' array_length / unnest / array_dims, so what is asserted is what the SERVER
' parsed, not what our own reader makes of our own writer. A round trip
' through the same code on both ends passes on a matched pair of bugs; the
' oracle does not. Every line states its expected value and prints ok or a
' MISMATCH naming both sides.
'
' HISTORY, because the file changed jobs. Until 2026-09-05 an array result
' raised and an array parameter went out as JSON, and this file was the
' NEGATIVE CONTROL for that -- it went red the moment native arrays landed,
' which is how the reference paragraph got rewritten instead of rotting. It
' is now the positive suite for what replaced it.
'
' THE AMBIGUITY THE DESIGN TURNS ON: `[["a","b"]]` is a JSON array for
' `$1::jsonb` and a Postgres array for `acl && $1`, and the wire text
' differs. The module PREPARES and DESCRIBES a statement that carries an
' array parameter and renders each one by the type Postgres inferred for its
' position -- so the jsonb contexts that worked before still get JSON (the
' CONTROL tier), and the array contexts get literals. Measured cost of the
' describe: 1.3x on a trivial local query, only when an array is passed.
load pg

db = pg.connect({})
tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' --- READ: every element kind, and the shapes a naive parser gets wrong ------
rows = pg.query(db, "select '{a,\"b c\",\"q\\\"uote\",\"back\\\\slash\",NULL,\"{brace}\",\"com,ma\",\"NULL\",\"\"}'::text[] as t, '{1,2,NULL}'::int[] as i, '{1.5,2.25}'::float8[] as f, '{t,f}'::bool[] as b, '{9007199254740993,1}'::int8[] as big, '{1.10,2.200}'::numeric[] as n, '{{1,2},{3,4}}'::int[] as nested, '{}'::text[] as empty, '[2:3]={x,y}'::text[] as lb", [])
r = rows[0]
check("text[] arrives as an array", type(r.t), "array")
check("  of the right length", count(r.t), 9)
check("  a quoted element with a space", r.t[1], "b c")
check("  an escaped quote", r.t[2], "q\"uote")
check("  an escaped backslash", r.t[3], "back\\slash")
check("  a bare NULL is nothing", is_nothing(r.t[4]), true)
check("  braces inside a quoted element", r.t[5], "{brace}")
check("  a comma inside a quoted element", r.t[6], "com,ma")
check("  the quoted TEXT \"NULL\" is a string, not null", r.t[7], "NULL")
check("  the empty string survives", r.t[8], "")
check("int[] elements are numbers", r.i[1], 2)
check("  and a NULL among them is nothing", is_nothing(r.i[2]), true)
check("float8[] elements are numbers", r.f[1], 2.25)
check("bool[] elements are booleans", r.b[0], true)
check("int8[] elements are STRINGS -- the exactness rule, per element", type(r.big[0]), "string")
check("  and the digits survive past 2^53", r.big[0], "9007199254740993")
check("numeric[] elements are strings too", r.n[1], "2.200")
check("a 2-D array is nested arrays, parsed not flattened", string(r.nested), "[[1,2],[3,4]]")
check("the empty array is an empty array", count(r.empty), 0)
check("a lower-bound prefix is skipped", string(r.lb), "[\"x\",\"y\"]")

' --- WRITE, WITH POSTGRES AS THE ORACLE -------------------------------------
hostile = ["plain", "b c", "q\"uote", "back\\slash", nothing, "{brace}", "com,ma", "NULL", ""]
rows = pg.query(db, "select array_length($1::text[], 1) as n", [hostile])
check("Postgres counts the elements we sent", rows[0].n, 9)
rows = pg.query(db, "select unnest($1::text[]) as e", [hostile])
check("  unnest gives them back in order: [2]", rows[2].e, "q\"uote")
check("  [3]", rows[3].e, "back\\slash")
check("  [5]", rows[5].e, "{brace}")
check("  [6]", rows[6].e, "com,ma")
check("  [8] the empty string", rows[8].e, "")
rows = pg.query(db, "select ($1::text[])[5] is null as real_null, ($1::text[])[8] = 'NULL' as text_null", [hostile])
check("nothing became a real NULL", rows[0].real_null, true)
check("  and the text NULL stayed text -- only quoting keeps these apart", rows[0].text_null, true)
rows = pg.query(db, "select array_length($1::int[],1) as ni, ($1::int[])[2] as second, array_length($2::bool[],1) as nb, array_dims($3::int[]) as dims, array_length($4::text[],1) as empty_len", [[10, 20, 30], [true, false], [[1, 2], [3, 4]], []])
check("int[] parameter: length", rows[0].ni, 3)
check("  and element 2", rows[0].second, 20)
check("bool[] parameter: length", rows[0].nb, 2)
check("nested parameter: Postgres sees two dimensions", rows[0].dims, "[1:2][1:2]")
check("empty parameter: array_length is NULL, as Postgres defines it", is_nothing(rows[0].empty_len), true)

' --- ROUND TRIP: write then read, whole-value equality -----------------------
pg.exec(db, "create temporary table gbasic_arrays (id int primary key, acl text[], nums float8[], flags bool[])", [])
pg.exec(db, "insert into gbasic_arrays values ($1, $2, $3, $4)", [1, hostile, [0.1, 0.2, 0.3], [true, nothing, false]])
rows = pg.query(db, "select acl, nums, flags from gbasic_arrays where id = 1", [])
check("text[] round trip is equal as a value", rows[0].acl = hostile, true)
check("float8[] round trip", rows[0].nums = [0.1, 0.2, 0.3], true)
check("bool[] with a NULL round trip", rows[0].flags = [true, nothing, false], true)

' --- THE QUERY THE PROPOSAL WAS WRITTEN AROUND ------------------------------
pg.exec(db, "insert into gbasic_arrays (id, acl) values (2, '{staff,lending}'), (3, '{staff,hr}'), (4, '{board}')", [])
rows = pg.query(db, "select id from gbasic_arrays where acl && $1 order by id", [["lending", "board"]])
check("acl && $1 -- the retrieval predicate, verbatim", string(rows[0].id) + "," + string(rows[1].id), "2,4")
rows = pg.query(db, "select id from gbasic_arrays where id = any($1) order by id", [[3, 4, 99]])
check("id = any($1)", count(rows), 2)

' --- CONTROL: everything that worked before still gets JSON ----------------
rows = pg.query(db, "select jsonb_typeof($1::jsonb) as kind, $1::jsonb as j", [["a", "b"]])
check("an array to a jsonb parameter is STILL JSON", rows[0].kind, "array")
check("  and decodes back", string(rows[0].j), "[\"a\",\"b\"]")
rows = pg.query(db, "select $1::jsonb -> 'k' as k", [{k: "v"}])
check("a record parameter is untouched", rows[0].k, "v")
rows = pg.query(db, "select $1 as v", [["a", "b"]])
check("an UNTYPED array parameter is JSON text (Postgres infers text)", rows[0].v, "[\"a\",\"b\"]")
pg.exec(db, "alter table gbasic_arrays add column acl_json jsonb", [])
pg.exec(db, "update gbasic_arrays set acl_json = to_jsonb(acl)", [])
rows = pg.query(db, "select id from gbasic_arrays where exists (select 1 from jsonb_array_elements_text(acl_json) a join jsonb_array_elements_text($1::jsonb) g on a = g) order by id", [["lending", "board"]])
check("the jsonb workaround the reference used to recommend still works", string(rows[0].id) + "," + string(rows[1].id), "2,4")

' --- REFUSALS, each beside its legal neighbour above -------------------------
on error goto next
rows = pg.query(db, "select $1::int[] as v", [[1, 2], [3]])
raised = false
message = ""
if error then
    raised = true
    message = error.message
    error.clear()
end if
check("too many parameters is refused at describe", raised, true)
check("  naming both counts", contains(message, "1 parameter") and contains(message, "2 were supplied"), true)
rows = pg.query(db, "select $1::int[] as v", [[1, "two", 3]])
raised = false
if error then
    raised = true
    error.clear()
end if
check("a text element into int[] is refused by Postgres, not silently zeroed", raised, true)
on error stop

' --- pgvector, unchanged by any of this ------------------------------------
have_vector = pg.query(db, "select count(*) as n from pg_extension where extname = 'vector'", [])
if string(have_vector[0].n) = "1" then
    pg.exec(db, "alter table gbasic_arrays add column vec vector(3)", [])
    pg.exec(db, "update gbasic_arrays set vec = nums::vector where id = 1", [])
    rows = pg.query(db, "select vec from gbasic_arrays where id = 1", [])
    check("a pgvector column still arrives as text", type(rows[0].vec), "string")
    check("  that decode reads", decode(rows[0].vec)[1], 0.2)
    rows = pg.query(db, "select id, vec <-> $1::vector as dist from gbasic_arrays where vec is not null order by dist limit 1", [encode([0.1, 0.2, 0.3])])
    check("  and encode(v)::vector still searches", number(rows[0].dist), 0)
else
    print "SKIP pgvector tiers (extension not installed in this database)"
end if

' --- COST: the describe round trip, as a ratio with a generous gate ----------
n = 200
t0 = monotonic()
i = 0
while i < n
    rows = pg.query(db, "select $1::int as v", [i])
    i = i + 1
end while
t1 = monotonic()
i = 0
while i < n
    rows = pg.query(db, "select ($1::int[])[1] as v", [[i]])
    i = i + 1
end while
t2 = monotonic()
ratio = (t2 - t1) / (t1 - t0)
check("prepare+describe costs under 5x a bare query (measured ~1.3x)", ratio < 5, true)

pg.close(db)
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
