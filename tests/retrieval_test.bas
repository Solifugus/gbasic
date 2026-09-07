' `retrieval` -- permission-filtered nearest-neighbour search over pgvector.
' Self-checking; run by tests/run_retrieval.sh against a real PostgreSQL.
'
' docs/gbasic_ai_reference_and_primitives.md §1.7, step 7.
'
' THE LOAD-BEARING TIER IS FILTER-THEN-RANK, and it is a DIFFERENCE. The
' obvious implementation ranks first and drops what the caller may not see, and
' it fails in a way that looks like an ordinary empty result: a user with narrow
' permissions asks a question, the globally nearest chunks all belong to someone
' else, every one is dropped, and they are told nothing matched. Nothing errors.
' Their OWN nearest chunks were never considered.
'
' So the fixture plants a corpus where the three nearest chunks to the query are
' ones the asking user MAY NOT SEE, and asserts they still get their own top-2.
' Its CONTROL is a user who MAY see them getting those three instead -- without
' it, "the narrow user got two rows" is equally satisfied by an ACL that does
' nothing at all.
'
' SELF-CHECKING, and forced: every defect here is a PLAUSIBLE RESULT SET. Rows
' come back, in a sensible order, with sensible distances -- they are simply the
' wrong rows, or the right rows for the wrong person.

load retrieval
load pg

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

function ids(rows)
    out = []
    for each r in rows
        append(out, r.id)
    end for
    return join(out, ",")
end function

db = pg.connect({})
x = pg.exec(db, "drop table if exists retrieval_probe", [])
retrieval.create(db, "retrieval_probe", 3)

' The query is [1,0,0]. r1..r3 are nearest; v1,v2 are further away. The asking
' user is in `everyone` and may see only v1 and v2.
chunks = [
    { id: "r1", source: "payroll.pdf", text: "salary bands",   hash: "h1", acl: ["finance"],  vec: [0.99, 0.10, 0] },
    { id: "r2", source: "payroll.pdf", text: "bonus policy",   hash: "h2", acl: ["finance"],  vec: [0.98, 0.15, 0] },
    { id: "r3", source: "payroll.pdf", text: "equity grants",  hash: "h3", acl: ["finance"],  vec: [0.97, 0.20, 0] },
    { id: "v1", source: "handbook.md", text: "holiday policy", hash: "h4", acl: ["everyone"], vec: [0.50, 0.80, 0] },
    { id: "v2", source: "handbook.md", text: "expenses",       hash: "h5", acl: ["everyone"], vec: [0.20, 0.90, 0] }
]
w = retrieval.store(db, "retrieval_probe", chunks)
check("every chunk is stored", w.written, 5)
check("and none was skipped on a first write", w.skipped, 0)

q = [1, 0, 0]

' The premise the whole tier rests on: the restricted chunks really ARE nearest.
' Without this the difference below could be an accident of the data.
all_rows = pg.query(db,
    "select id from retrieval_probe order by vec <-> $1::vector limit 3",
    [ "[1,0,0]" ])
check("PREMISE: the three globally nearest chunks are the restricted ones",
      ids(all_rows), "r1,r2,r3")

' THE DIFFERENCE.
narrow = retrieval.search(db, "retrieval_probe", q, ["everyone"], 2)
check("a narrowly permitted user gets THEIR OWN top-2", ids(narrow), "v1,v2")
check("not an empty result, which is what rank-then-filter returns here",
      count(narrow), 2)

' THE CONTROL. Without it, the check above passes on an ACL that filters
' everything, or on one that does nothing and happens to order differently.
wide = retrieval.search(db, "retrieval_probe", q, ["finance"], 2)
check("a user who may see them gets the nearest two instead", ids(wide), "r1,r2")
check("so the two callers get DIFFERENT rows from one query and one corpus",
      ids(narrow) != ids(wide), true)

' Someone in both sees the true nearest.
both = retrieval.search(db, "retrieval_probe", q, ["finance", "everyone"], 5)
check("a caller in both groups sees everything, nearest first",
      ids(both), "r1,r2,r3,v1,v2")

' A caller in no matching group sees nothing -- which is a real answer, and the
' one that must NOT be produced for the narrow user above.
none = retrieval.search(db, "retrieval_probe", q, ["contractors"], 5)
check("a caller with no overlapping group sees nothing at all", count(none), 0)
check("and an empty group list likewise", count(retrieval.search(db, "retrieval_probe", q, [], 5)), 0)

' Distances come back and are ordered. GUARDED: on a broken build `narrow` is
' empty, and indexing it would RAISE -- so the fixture would stop here and
' report "did not run" instead of the mismatches it had already found, which is
' the wrong diagnosis at the worst moment.
if count(narrow) >= 2 then
    check("the rows carry their distance", is_number(narrow[0].distance), true)
    check("nearest first", narrow[0].distance <= narrow[1].distance, true)
    check("and the row carries what a citation needs", narrow[0].source, "handbook.md")
else
    check("the rows carry their distance (skipped: no rows came back)", false, true)
    check("nearest first (skipped: no rows came back)", false, true)
    check("and the row carries what a citation needs (skipped)", false, true)
end if

' ---- content-hash keying, which is what makes an indexer resumable ---------
again = retrieval.store(db, "retrieval_probe", chunks)
check("re-storing unchanged chunks writes nothing", again.written, 0)
check("and reports them skipped", again.skipped, 5)

changed = [ { id: "v1", source: "handbook.md", text: "holiday policy (2026)",
              hash: "h4-new", acl: ["everyone"], vec: [0.50, 0.80, 0] } ]
upd = retrieval.store(db, "retrieval_probe", changed)
check("a changed hash IS written", upd.written, 1)
after = retrieval.search(db, "retrieval_probe", q, ["everyone"], 1)
if count(after) >= 1 then
    check("and the new text is what comes back", after[0].text, "holiday policy (2026)")
else
    check("and the new text is what comes back (no rows)", false, true)
end if

' ---- refusals --------------------------------------------------------------
on error goto next

y = retrieval.search(db, "retrieval_probe; drop table x", q, ["everyone"], 1)
if error then
    check("an unsafe table name is refused rather than escaped",
          contains(error.message, "may use only lowercase letters"), true)
    error.clear()
end if

y = retrieval.search(db, "retrieval_probe", q, "everyone", 1)
if error then
    check("groups must be an array", contains(error.message, "groups as an array"), true)
    error.clear()
end if

y = retrieval.search(db, "retrieval_probe", q, ["everyone"], 0)
if error then
    check("the limit must be positive", contains(error.message, "positive limit"), true)
    error.clear()
end if

y = retrieval.search(db, "retrieval_probe", "not a vector", ["everyone"], 1)
if error then
    check("a vector must be an array of numbers",
          contains(error.message, "array of numbers"), true)
    error.clear()
end if

y = retrieval.store(db, "retrieval_probe", [ { id: "z", source: "s", text: "t", hash: "h", vec: [1,0,0] } ])
if error then
    check("a chunk with no acl is refused -- an unset acl is not a public one",
          contains(error.message, "has no 'acl'"), true)
    error.clear()
end if

' The control: the same calls, well formed, still work.
ok = retrieval.search(db, "retrieval_probe", q, ["everyone"], 1)
check("the control: a well-formed search still answers", count(ok) > 0, true)

x = pg.exec(db, "drop table if exists retrieval_probe", [])
pg.close(db)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
