' max_rows on pg.query and odbc.query (gdash's GDASH-16 B2): past the cap a
' query RAISES, with details { code: "MAX_ROWS", max_rows }, rather than
' answering a partial result. Arguments: "pg" or "odbc", and for odbc the
' connection string. Self-checking.
function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    which = args[0]
    if which = "pg" then
        load pg
        db = pg.connect({})
        many = "select g from generate_series(1, 5000000) g"
        five = "select g from generate_series(1, 5) g"
        param = "select $1::int + 1 as x"
    else
        load odbc
        db = odbc.connect(args[1])
        many = "with recursive c(x) as (select 1 union all select x + 1 from c where x < 5000000) select x from c"
        five = "with recursive c(x) as (select 1 union all select x + 1 from c where x < 5) select x from c"
        param = "select ? + 1 as x"
    end if
    function q(sql, params, options)
        if which = "pg" then
            return pg.query(db, sql, params, options)
        end if
        return odbc.query(db, sql, params, options)
    end function
    check("exactly the cap is answered   ", len(q(five, [], { max_rows: 5 })), 5)
    r = try q(five, [], { max_rows: 4 })
    check("one past the cap raises       ", error.details.code, "MAX_ROWS")
    check("and names the cap             ", error.details.max_rows, 4)
    check("in words                      ", error.message, which + ".query: the query returned more than 4 rows, its max_rows")
    r = try q(many, [], { max_rows: 1000 })
    check("a big query stops at the cap  ", error.details.code, "MAX_ROWS")
    check("and the connection still works", len(q(five, [], { max_rows: 10 })), 5)
    check("with parameters               ", q(param, [41], { max_rows: 1 })[0].x, 42)
    r = try q(five, [], { max_row: 3 })
    check("an unknown option is refused  ", error.message, which + ".query: unknown option 'max_row' (the options are: max_rows)")
    r = try q(five, [], { max_rows: 0 })
    check("a cap of 0 is refused         ", error.message, which + ".query: max_rows must be a whole number, at least 1")
end program
