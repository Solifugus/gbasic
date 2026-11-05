' Savepoints, as nested transactions (gap G8 from the gBASIC books): a BEGIN
' inside a write transaction is a savepoint, its COMMIT keeps what it did, its
' ROLLBACK undoes that and nothing before; and a statement refused part way
' inside a transaction is undone alone, leaving the transaction usable. Every
' answer is written here by hand. Run by tests/run_gsql_module.sh from inside an
' empty directory.
load gsql

checks = { n: 0, bad: 0 }

database blk( file: "blk.gsql", create: true )

' two statements, the second refused: inside a program's transaction the block
' owns a savepoint now, so its first statement is undone with it
sql b_half on blk
    insert into w values (2, 'from the block');
    insert into w values (1, 'a duplicate')
end sql

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

' ok, or the code a statement or call was refused with
function try_exec(db, sql)
    on error goto refused
    r = gsql.exec(db, sql)
    return "ok"
refused:
    c = "(no code)"
    if has(error.details, "code") then
        c = error.details.code
    end if
    error.clear()
    return c
end function

function try_begin(db, mode)
    on error goto refused
    r = gsql.begin(db, mode)
    return "ok"
refused:
    c = error.details.code
    error.clear()
    return c
end function

function try_commit(db)
    on error goto refused
    r = gsql.commit(db)
    return "ok"
refused:
    c = error.details.code
    error.clear()
    return c
end function

function ks(db)
    out = []
    for each r in gsql.query(db, "select k from t order by k")
        append(out, r.k)
    end for
    return out
end function

db = gsql.connect("sp.gsql", { create: true })
gsql.exec(db, "create table t (k integer primary key, v text)")

' --- the matching ROLLBACK of a nested BEGIN undoes only its own work ----------
gsql.begin(db)
gsql.exec(db, "insert into t values (1, 'outer')")
gsql.begin(db)
gsql.exec(db, "insert into t values (2, 'inner')")
gsql.exec(db, "update t set v = 'changed' where k = 1")
check("inside the savepoint both rows show", ks(db), [1, 2])
gsql.rollback(db)
check("after the nested rollback, only the outer row", ks(db), [1])
check("  and the outer row's update is undone too", gsql.query(db, "select v from t where k = 1")[0].v, "outer")
gsql.commit(db)
check("committed: the outer work alone", ks(db), [1])

' --- the matching COMMIT keeps it, inside the transaction ---------------------
gsql.begin(db)
gsql.begin(db)
gsql.exec(db, "insert into t values (3, 'kept')")
gsql.commit(db)
check("a nested commit keeps its row inside the transaction", ks(db), [1, 3])
gsql.rollback(db)
check("and the outer rollback still undoes it", ks(db), [1])

' --- three deep ---------------------------------------------------------------
gsql.begin(db)
gsql.exec(db, "insert into t values (10, 'a')")
gsql.begin(db)
gsql.exec(db, "insert into t values (11, 'b')")
gsql.begin(db)
gsql.exec(db, "insert into t values (12, 'c')")
gsql.rollback(db)
gsql.begin(db)
gsql.exec(db, "insert into t values (13, 'd')")
gsql.commit(db)
gsql.commit(db)
gsql.commit(db)
check("three deep: the innermost rollback took 12 alone", ks(db), [1, 10, 11, 13])

' --- a schema change undone ----------------------------------------------------
gsql.exec(db, "create table keep (k integer primary key)")
gsql.exec(db, "insert into keep values (7), (8)")
gsql.begin(db)
gsql.begin(db)
gsql.exec(db, "drop table keep")
gsql.exec(db, "create table fresh (k integer)")
gsql.rollback(db)
gsql.commit(db)
check("a DROP undone by a savepoint leaves the table and its rows", len(gsql.query(db, "select k from keep")), 2)
check("a CREATE undone by one leaves no table", try_exec(db, "select k from fresh"), "NOT_FOUND")

' --- a statement refused part way is undone alone --------------------------------
gsql.begin(db)
gsql.exec(db, "insert into t values (20, 'before')")
check("a multi-row insert meeting a duplicate on its third row",
      try_exec(db, "insert into t values (21, 'x'), (22, 'y'), (1, 'dup'), (23, 'z')"), "DUPLICATE")
check("  leaves the transaction usable", try_exec(db, "insert into t values (24, 'after')"), "ok")
check("  and its first two rows are not there", ks(db), [1, 10, 11, 13, 20, 24])
gsql.commit(db)
check("committed without the refused statement", ks(db), [1, 10, 11, 13, 20, 24])

' --- an update refused part way ------------------------------------------------
gsql.exec(db, "create table u (k integer primary key, n integer unique)")
gsql.exec(db, "insert into u values (1, 10), (2, 20), (3, 30)")
gsql.begin(db)
check("an update that collides on a later row", try_exec(db, "update u set n = 20 where k <> 2"), "DUPLICATE")
check("  changed nothing", gsql.query(db, "select n from u where k = 1")[0].n, 10)
gsql.exec(db, "update u set n = 11 where k = 1")
gsql.commit(db)
check("  and the transaction committed what came after", gsql.query(db, "select n from u where k = 1")[0].n, 11)

' --- a serial undone takes its number back -------------------------------------
gsql.exec(db, "create table s (id integer serial primary key, what text)")
gsql.begin(db)
gsql.exec(db, "insert into s (what) values ('one')")
gsql.begin(db)
gsql.exec(db, "insert into s (what) values ('undone')")
gsql.rollback(db)
gsql.exec(db, "insert into s (what) values ('two')")
gsql.commit(db)
ids = []
for each r in gsql.query(db, "select id from s order by id")
    append(ids, r.id)
end for
check("a serial is gapless across an undone savepoint", ids, [1, 2])

' --- what is refused ----------------------------------------------------------------
gsql.begin(db, "read")
check("a write BEGIN inside a read transaction", try_begin(db, "write"), "READ_ONLY")
check("a read BEGIN inside one nests", try_begin(db, "read"), "ok")
gsql.rollback(db)
gsql.rollback(db)
check("nothing is open after both", try_commit(db), "STATE")

' --- a multi-statement sql block inside a transaction owns a savepoint ---------
' It used to JOIN the program's transaction, so a block refused at its second
' statement left its first statement's row behind in it.
function try_block()
    on error goto refused
    r = gsql.exec(b_half)
    return "ok"
refused:
    c = error.details.code
    error.clear()
    return c
end function
gsql.exec(blk, "create table w (k integer primary key, v text)")
gsql.begin(blk)
gsql.exec(blk, "insert into w values (1, 'program')")
check("the block is refused at its second statement", try_block(), "DUPLICATE")
check("  and its first statement's row is undone with it", len(gsql.query(blk, "select k from w where k = 2")), 0)
check("  while the program's row stays", len(gsql.query(blk, "select k from w where k = 1")), 1)
check("  and the program's transaction commits", try_commit(blk), "ok")

print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
