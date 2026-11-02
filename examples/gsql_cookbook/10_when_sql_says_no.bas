' Recipe 10: a refusal is an ordinary gBASIC error, with G-SQL's code by name
' and, where the statement has a place to point at, its position.
'
' This recipe sends deliberately broken SQL, so it uses strings: in an `sql`
' block a syntax error is refused when the program LOADS, at its line and
' column, before anything runs -- which is why blocks are the normal form, and
' why they cannot show a syntax error being caught at run time.
load gsql

work = temp_dir()
db = gsql.connect(join_path(work, "e.gsql"), { create: true })
db.exec("create table t (id integer primary key, name text)")

function explain_error(sql)
    on error goto next
    db.query(sql)
    if error then
        where = ""
        if has(error.details, "position") then
            where = " at character " + string(error.details.position)
        end if
        print error.details.code + where + ": " + sql
    end if
    return nothing
end function

explain_error("select nme from t")             ' NOT_FOUND: no column nme
explain_error("selct * from t")                ' SYNTAX

' A comparison is checked when a row reaches it. Over an empty table nothing
' reaches it, so a query that will be refused later answers nothing now:
explain_error("select * from t where name = 1")
print "  (said nothing: the table is empty)"
db.exec("insert into t values (1, 'one')")
explain_error("select * from t where name = 1")   ' EXPRESSION: no implicit casts
explain_error("select * from t where name = '1'") ' fine: says nothing

' and a query of a statement that returns nothing: gsql.query is for rows,
' so it is refused -- saying that the statement ran, since it did.
' (gsql.exec runs anything, and answers rows too when there are any.)
function wrong_verb()
    on error goto next
    db.query("update t set name = 'uno' where id = 1")
    if error then
        print error.details.code + ": " + error.message
    end if
    return nothing
end function
wrong_verb()

db.close()
remove_tree(work)
