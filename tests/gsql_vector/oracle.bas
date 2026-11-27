' The G-SQL half of run_gsql_vector.sh's oracle tier. Reads the table and
' queries tests/gsql_vector/oracle.py wrote (args[0]), stores the rows, runs
' each query through SQL in the spelling it names, and prints each answer;
' oracle.py decides from Python's own arithmetic whether it is right. Vectors
' arrive as the hex of their float32 bytes, so nothing is lost to printing.
'   query NAME METRIC SPELLING ACL K Q  ->  a NAME ID DISTANCE, nearest first
load gsql

program main(args)
    db = gsql.connect("oracle.gsql", { create: true })
    rows = []
    for each line in {file}args[0]
        f = split(line, " ")
        if f[0] = "dim" then
            db.exec("create table t (id integer primary key, acl text, e vector(" + f[1] + "))")
        else if f[0] = "row" then
            append(rows, { id: number(f[1]), acl: f[2], e: vector_from_bytes(hex_decode(f[3])) })
        else
            if len(rows) > 0 then
                gsql.insert(db, "t", rows)
                rows = []
            end if
            q = vector_from_bytes(hex_decode(f[6]))
            measure = ""
            if f[3] = "distance" then
                measure = "distance(e, ?, '" + f[2] + "')"
            else if left(f[3], 1) = "<" then
                measure = "e " + f[3] + " ?"
            else
                measure = f[3] + "(e, ?)"
            end if
            where = ""
            params = [q, q]
            if f[4] != "*" then
                where = " where acl = ?"
                params = [q, f[4], q]
            end if
            sql = "select id, " + measure + " as d from t" + where + " order by " + measure + " limit " + f[5]
            for each r in db.query(sql, params)
                print "a " + f[1] + " " + string(r.id) + " " + string(r.d)
            next
        end if
    next
end program
