' What `pg` does with a set-valued column, held to what docs/reference.md says.
'
' SELF-CHECKING, and half of it is a NEGATIVE CONTROL. The reference documents
' that Postgres's native array types are unsupported in both directions and
' that a `jsonb` column plus pgvector's own type carry the same data on the
' module as it is. Every line here states what it expects and prints ok or a
' MISMATCH naming both sides. The two "raises" checks are the control in the
' run_limitations.sh sense: they go RED when native arrays are implemented,
' which is the signal to rewrite the paragraph in reference.md rather than to
' fix the test.
'
' Found 2026-09-05 while checking the AI reference proposal against the tree.
' The first version of that finding was reasoned from the raise and concluded
' native arrays blocked the design; running it -- this file's ancestor --
' showed the jsonb/vector schema works today and reversed the conclusion.
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

pg.exec(db, "create temporary table gbasic_arrays (id integer primary key, text text, acl text[], acl_json jsonb, vec_arr float8[])", [])
pg.exec(db, "insert into gbasic_arrays values (1, 'lending policy', '{staff,lending}', '[\"staff\",\"lending\"]', '{0.1,0.2,0.3}')", [])
pg.exec(db, "insert into gbasic_arrays values (2, 'hr handbook', '{staff,hr}', '[\"staff\",\"hr\"]', '{0.9,0.1,0.0}')", [])
pg.exec(db, "insert into gbasic_arrays values (3, 'board minutes', '{board}', '[\"board\"]', '{0.0,0.0,1.0}')", [])

' --- NEGATIVE CONTROL: native arrays are unsupported, as documented ---------
' NOTE `raised = error` would CLAIM the pending error (reading `error` is the
' acknowledgement), so the flag is set inside the `if` and the message is
' read there too.
on error goto next
rows = pg.query(db, "select id, acl from gbasic_arrays order by id", [])
raised = false
message = ""
if error then
    raised = true
    message = error.message
    error.clear()
end if
check("a text[] result column raises (documented; red means native arrays landed)", raised, true)
check("  with the documented message", contains(message, "array result types are not supported"), true)

rows = pg.query(db, "select id, vec_arr from gbasic_arrays order by id", [])
raised = false
if error then
    raised = true
    error.clear()
end if
check("a float8[] result column raises too", raised, true)

rows = pg.query(db, "select id from gbasic_arrays where acl && $1", [["staff", "lending"]])
raised = false
message = ""
if error then
    raised = true
    message = error.message
    error.clear()
end if
check("an array parameter is refused by Postgres (it went out as JSON)", raised, true)
check("  and Postgres names the malformed literal", contains(message, "malformed array literal"), true)
on error stop

' --- THE ALTERNATIVE THAT WORKS, as documented -------------------------------
rows = pg.query(db, "select id, text from gbasic_arrays where exists (select 1 from jsonb_array_elements_text(acl_json) a join jsonb_array_elements_text($1::jsonb) g on a = g) order by id", [["staff", "lending"]])
check("a jsonb column takes the JSON a parameter already is: rows matched", count(rows), 2)
check("  the right rows", rows[0].text + "/" + rows[1].text, "lending policy/hr handbook")
rows = pg.query(db, "select id from gbasic_arrays where exists (select 1 from jsonb_array_elements_text(acl_json) a join jsonb_array_elements_text($1::jsonb) g on a = g)", [["nobody"]])
check("  and a group nobody has matches nothing", count(rows), 0)

' A jsonb RESULT decodes to an array, so the set comes back as a set.
rows = pg.query(db, "select acl_json from gbasic_arrays where id = 1", [])
check("a jsonb result decodes to an array", type(rows[0].acl_json), "array")
check("  of the right length", count(rows[0].acl_json), 2)

' --- pgvector, if the extension is installed in this database ----------------
have_vector = pg.query(db, "select count(*) as n from pg_extension where extname = 'vector'", [])
if have_vector[0].n = "1" or have_vector[0].n = 1 then
    pg.exec(db, "alter table gbasic_arrays add column vec vector(3)", [])
    pg.exec(db, "update gbasic_arrays set vec = vec_arr::vector", [])
    rows = pg.query(db, "select vec from gbasic_arrays where id = 1", [])
    check("a pgvector column arrives as text", type(rows[0].vec), "string")
    v = decode(rows[0].vec)
    check("  which decode reads as an array", type(v), "array")
    check("  with the stored values", v[1], 0.2)
    rows = pg.query(db, "select text, vec <-> $1::vector as dist from gbasic_arrays order by dist limit 1", [encode([0.0, 0.0, 1.0])])
    check("a vector sent back as encode(v)::vector finds the nearest row", rows[0].text, "board minutes")
    check("  at distance zero", number(rows[0].dist), 0)
else
    print "SKIP pgvector tiers (extension not installed in this database)"
end if

pg.close(db)
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
