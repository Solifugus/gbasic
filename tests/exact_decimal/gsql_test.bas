' EXACT DECIMALS THROUGH serialize AND G-SQL (docs/exact_decimal_design.md
' increment 2): a decimal travels as its digits (exactness byte 2), a G-SQL
' `number` column holds one exactly, and a dump restores it as a decimal while a
' double stays a double. Self-checking; the directory is the first argument.
load gsql

function check(label, got, want)
    if string(got) = string(want) then
        print "ok   " + label
    else
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main( args )
    dir = args[0]
    print "-- serialize"
    check("18 places through the bytes ", deserialize(serialize(0.123456789012345678)) = 0.123456789012345678, true)
    check("and the double beside it    ", deserialize(serialize(1e-1)) = 1e-1, true)
    check("a decimal stays exact       ", deserialize(serialize(6331.475)) - 2182.855, "4148.62")
    check("a double stays a double     ", deserialize(serialize(1e-1)) + 2e-1, "0.30000000000000004")
    check("nested                      ", deserialize(serialize({ a: [0.05] })).a[0] * 3, "0.15")
    check("one representation          ", serialize(0.5 + 0.5) = serialize(1.0), true)

    print "-- a G-SQL number column"
    db = gsql.connect(dir + "/d.gsql", { create: true })
    db.exec("create table q (id integer primary key, qty number)")
    db.exec("create index q_qty on q (qty)")
    db.insert("q", [{ id: 1, qty: 6331.475 }, { id: 2, qty: 2182.855 }, { id: 3, qty: 0.123456789012345678 },
                    { id: 4, qty: 1e-1 }, { id: 5, qty: 0.1 }])
    rows = db.query("select id, qty from q order by id")
    check("read back exact             ", rows[0].qty - rows[1].qty, "4148.62")
    check("past a double's digits      ", rows[2].qty, "0.123456789012345678")
    check("SQL arithmetic is exact     ", db.query("select qty - 2182.855 as d from q where id = 1")[0].d, "4148.62")
    check("SQL sum is exact            ", db.query("select sum(qty) as s from q where id <= 2")[0].s, "8514.33")
    check("a seek by a decimal         ", count(db.query("select id from q where qty = 6331.475")), 1)
    check("a decimal and its double    ", count(db.query("select id from q where qty = 0.1")), 2)
    check("a range                     ", count(db.query("select id from q where qty > 0.1 and qty < 3000")), 2)
    r = try db.exec("create table i (n integer)")
    r = try db.insert("i", [{ n: 2.0 }])
    check("an integer column refuses 2.0", error.details.code, "TYPE")

    print "-- dump and restore"
    db.dump(dir + "/d.sql")
    db2 = gsql.connect(dir + "/e.gsql", { create: true })
    db2.restore(dir + "/d.sql")
    back = db2.query("select id, qty from q order by id")
    check("the decimal restores exact  ", back[2].qty, "0.123456789012345678")
    check("and stays a decimal         ", back[0].qty - back[1].qty, "4148.62")
    check("the double restores a double", serialize(back[3].qty) = serialize(1e-1), true)
    check("the same bytes, row by row  ", serialize(back) = serialize(rows), true)
    db.close()
    db2.close()
end program
