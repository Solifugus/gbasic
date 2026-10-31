' G-SQL's RANDOM draws every value equally often (docs/gsql_generated_design.md
' §3). Run by tests/run_gsql_module.sh with GSQL_DIR set to an empty directory.
'
' An alphabet of up to 256 characters is drawn a byte at a time, so with 200
' characters a plain "byte mod 200" would make the first 56 twice as likely as
' the rest: 112 of every 256 draws, 0.4375, where a fair draw gives 56 of 200,
' 0.28. Some 20,000 draws put the fair share within about 0.003 of 0.28, so a
' band of 0.03 either side does not flake and still sits far from 0.4375: it
' checks the step that discards the uneven tail, not only that drawing works.
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

db = gsql.connect(env("GSQL_DIR") + "/r.gsql", { create: true })

' 200 characters past Latin-1, each its own codepoint
alphabet = ""
index = {}
for i = 0 to 199
    ch = chr(256 + i)
    alphabet = alphabet + ch
    index[ch] = i
next

gsql.exec(db, "create table draws (id integer serial primary key, s text random(100, '" + alphabet + "'))")
gsql.begin(db)
for i = 1 to 200
    gsql.exec(db, "insert into draws (id) values (default)")
next
gsql.commit(db)

total = 0
low = 0
outside = 0
for each row in gsql.query(db, "select s from draws")
    if len(row.s) != 100 then
        outside = outside + 1
    end if
    for k = 0 to len(row.s) - 1
        ch = mid(row.s, k, 1)
        if has(index, ch) then
            total = total + 1
            if index[ch] < 56 then
                low = low + 1
            end if
        else
            outside = outside + 1
        end if
    next
next
check("every draw is 100 characters of the alphabet", outside, 0)
check("20,000 characters drawn", total, 20000)
share = low / total
check("the first 56 of 200 characters drawn their fair share, 0.28 +/- 0.03 (" + string(share) + ")",
      share > 0.25 and share < 0.31, true)

' an n-digit integer has n digits: none below 10^(n-1), none at 10^n
gsql.exec(db, "create table digits (id integer serial primary key, d integer random(3))")
gsql.begin(db)
for i = 1 to 400
    gsql.exec(db, "insert into digits (id) values (default)")
next
gsql.commit(db)
r = gsql.query(db, "select min(d) as lo, max(d) as hi, count(distinct d) as n from digits")[0]
check("RANDOM(3) never has fewer than three digits", r.lo >= 100, true)
check("RANDOM(3) never has more than three digits", r.hi <= 999, true)
check("RANDOM(3) is not one value repeated", r.n > 300, true)

gsql.close(db)
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
