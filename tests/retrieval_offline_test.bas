' The half of `retrieval` that needs no database, so this suite always asserts
' something even where PostgreSQL is not available. An entirely skippable suite
' is one that can go quiet.
'
' What it can establish is STRUCTURAL: the permission predicate is in the WHERE
' and the distance ordering follows it. That is weak on its own -- it is a fact
' about a string -- which is why the live tier proves the same thing
' behaviourally, by showing two callers getting different rows from one corpus.
' Neither tier replaces the other.

load retrieval

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

q = retrieval.query_text("chunks")
' `find` returns `nothing` on a miss, and comparing that to a number RAISES
' (PLAT-EQ), so the positions are normalised before they are ordered -- a
' fixture that raises on a broken build reports "did not run" instead of naming
' what broke, which is the wrong diagnosis at the worst moment.
where_at = find(q, "where acl &&")
order_at = find(q, "order by")
check("the query filters on the acl", contains(q, "where acl &&"), true)
check("and orders by distance", contains(q, "order by"), true)
before = false
if contains(q, "where acl &&") and contains(q, "order by") then
    before = where_at < order_at
end if
check("THE PREDICATE COMES BEFORE THE ORDERING, so the database narrows first",
      before, true)
check("the ordering is by the distance the query computes",
      contains(q, "vec <-> $1::vector as distance"), true)
check("and the acl is matched with array overlap, not a scan",
      contains(q, "acl && $2"), true)

d = retrieval.schema("chunks", 1536)
check("the schema is returned as statements, not run", is_array(d), true)
check("it creates the extension first", contains(d[0], "create extension"), true)
check("declares the vector with its dimension", contains(d[1], "vector(1536)"), true)
check("the acl as a text array", contains(d[1], "acl text[] not null"), true)
check("and indexes the acl, since it is what every query filters on",
      contains(d[2], "using gin (acl)"), true)

on error goto next
x = retrieval.query_text("chunks; drop table users")
if error then
    check("an unsafe identifier is refused rather than escaped",
          contains(error.message, "may use only lowercase letters"), true)
    error.clear()
end if
x = retrieval.query_text("Chunks")
if error then
    check("and so is one that merely looks unusual, because the rule is a fact "
          + "and not a judgement", contains(error.message, "may use only"), true)
    error.clear()
end if
x = retrieval.query_text("2chunks")
if error then
    check("a leading digit is refused", contains(error.message, "may not start with a digit"), true)
    error.clear()
end if
x = retrieval.schema("chunks", 0)
if error then
    check("a zero dimension is refused", contains(error.message, "positive number"), true)
    error.clear()
end if
ok = retrieval.query_text("chunks_v2")
check("the control: an ordinary identifier is accepted", contains(ok, "chunks_v2"), true)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
