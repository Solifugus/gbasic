' `error.details.table`, `.column` and `.constraint` beside `code`
' (docs/gsql_error_details_design.md; gbasic-books volume 3, gap G2). Run by
' tests/run_gsql_module.sh from inside an empty directory.
'
' One refused row of each kind, each name asserted by VALUE and each one that
' does not apply asserted ABSENT, so a name left over from the refusal before
' cannot pass as this one's: the cases alternate between kinds that name a
' column and kinds that do not.
load gsql

checks = { n: 0, bad: 0 }

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

' what the refusal said about itself: code, then each name or "-" when absent
function about(sql, params)
    on error goto next
    r = db.exec(sql, params)
    e = error
    if not e then
        return "not refused"
    end if
    out = e.details.code
    for each k in ["table", "column", "constraint"]
        if has(e.details, k) then
            out = out + " " + k + "=" + e.details[k]
        else
            out = out + " " + k + "=-"
        end if
    end for
    return out
end function

db = gsql.connect("details.gsql", { create: true })
db.exec("create table supplier (code string primary key)")
db.exec("create table item (sku string primary key, name string not null, price money(USD), eff date, qty integer, supplier string references supplier (code), constraint qty_positive check (qty > 0))")
db.exec("create unique index item_name on item (name)")
db.exec("create table lot (id integer primary key, sku string, constraint lot_item foreign key (sku) references item (sku))")
db.exec("insert into supplier (code) values ('S1')")
db.exec("insert into item (sku, name, price, eff, qty, supplier) values ('A-1', 'Gripper', 5, '2026-01-01', 1, 'S1')")
db.exec("insert into lot (id, sku) values (1, 'A-1')")

ins = "insert into item (sku, name, price, eff, qty, supplier) values ($1, $2, $3, $4, $5, $6)"
check("TYPE: a date that is not one", about(ins, ["A-2", "Servo", 3, "2026-13-01", 1, "S1"]), "TYPE table=item column=eff constraint=-")
check("CHECK: a quantity that is not positive", about(ins, ["A-3", "Sensor", 3, "2026-01-01", -1, "S1"]), "CHECK table=item column=- constraint=qty_positive")
check("TYPE: an integer that is not exact", about(ins, ["A-4", "Arm", 2, "2026-01-01", 2.5, "S1"]), "TYPE table=item column=qty constraint=-")
check("DUPLICATE: the primary key", about(ins, ["A-1", "Other", 2, "2026-01-01", 1, "S1"]), "DUPLICATE table=item column=- constraint=primary key")
check("NOT_NULL: a name that is missing", about(ins, ["A-5", unknown, 2, "2026-01-01", 1, "S1"]), "NOT_NULL table=item column=name constraint=-")
check("DUPLICATE: a unique index", about(ins, ["A-6", "Gripper", 2, "2026-01-01", 1, "S1"]), "DUPLICATE table=item column=- constraint=item_name")
check("TYPE: money in the wrong currency", about(ins, ["A-7", "Clamp", {EUR}"2.00", "2026-01-01", 1, "S1"]), "TYPE table=item column=price constraint=-")
check("FOREIGN_KEY: a supplier that is not there", about(ins, ["A-8", "Vise", 2, "2026-01-01", 1, "S9"]), "FOREIGN_KEY table=item column=- constraint=item_supplier_fkey")
check("FOREIGN_KEY: deleting a parent still referenced is about the parent", about("delete from item where sku = $1", ["A-1"]), "FOREIGN_KEY table=item column=- constraint=lot_item")
check("a refusal about no table names none", about("select 1 where 'a' = 1", []), "EXPRESSION table=- column=- constraint=-")
check("and SYNTAX none", about("selec 1", []), "SYNTAX table=- column=- constraint=-")

' a block's refusal carries them too
function via_block()
    on error goto next
    bad_row.exec({ sku: "B-1", qty: -5 })
    e = error
    return e.details.code + " " + e.details.table + " " + e.details.constraint
end function
database shop( file: "details.gsql" )
sql bad_row on shop
    insert into item (sku, name, price, eff, qty, supplier) values (:sku, 'Block', 2, '2026-01-01', :qty, 'S1')
end sql
check("an sql block's refusal names them", via_block(), "CHECK item qty_positive")
shop.close()
db.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
