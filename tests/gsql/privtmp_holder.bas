' The PRIVATE /tmp tier of run_gsql_module.sh (gdash's A1): a service under
' systemd's PrivateTmp holds a database open, as gdash's server does, while
' other processes, each with a /tmp of its own, must reach the same owner.
'
' args: [database, ready file, stop file]
load gsql
program main(args)
    db = gsql.connect(args[0], { create: true })
    db.exec("create table t (a number)")
    db.exec("insert into t values (1)")
    write(args[1], "ready")
    while not exists(args[2])
        sleep(1)
    end while
    db.close()
end program
