' The other side of privtmp_holder.bas: a process with its own /tmp and its
' own runtime directory, or none, reaching the database the holder serves.
'
' args: [database]
load gsql
program main(args)
    db = gsql.connect(args[0])
    db.exec("insert into t values (2)")
    rows = db.query("select count(*) as n from t")
    print("rows " + string(rows[0].n))
    db.exec("delete from t where a = 2")
    db.close()
end program
