' An odbc and an ldap connection take their module's verbs as methods
' (docs/handle_methods_design.md). odbc runs over the SQLite3 driver the
' runner names; ldap binds to a port nothing listens on, which answers
' `unreachable` as a value, so neither needs a server.
load odbc
load ldap

function check(what, got, want)
    if string(got) = string(want) and type(got) = type(want) then
        print "ok " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    db = odbc.connect(args[0])
    r = db.exec("create table t (n integer)")
    r = db.exec("insert into t values (?)", [7])
    check("odbc: a connection's query", len(db.query("select n from t")), 1)
    check("odbc: the module form agrees", string(odbc.query(db, "select n from t")), string(db.query("select n from t")))
    c = db.close()

    dir = ldap.connect({ host: "127.0.0.1", port: 9, security: "plain" })
    b = dir.bind("cn=nobody", "secret")
    check("ldap: a connection's bind", b.reason, "unreachable")
    check("ldap: the module form agrees", ldap.bind(dir, "cn=nobody", "secret").reason, b.reason)
    c = dir.close()
    print "done"
end program
