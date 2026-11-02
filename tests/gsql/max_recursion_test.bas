' `bank.max_recursion`: WITH RECURSIVE's round limit, an attribute of the
' database a program sets before running its statements
' (docs/gsql_recursive_design.md §3; Matthew's ruling 2026-11-02, "if it was my
' test.bas program then `bank.max_recursion = 2000`"). Run by
' tests/run_gsql_module.sh from inside an empty directory.
'
' Each limit is asserted as a difference: the same statement answering under
' one setting and refused under another, so a setting that never reached the
' owner cannot pass.
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

' the refusal's code when the block raises, or "none"
function refused(block)
    on error goto next
    rows = block.query()
    if error then
        return error.details.code
    end if
    return "none"
end function

database bank( file: "bank.gsql", create: true )

' 5 000 rounds, and 1 500
sql long_series on bank
    with recursive c (i) as (select 1 union all select i + 1 from c where i < 5001)
    select count(*) as n from c
end sql
sql short_series on bank
    with recursive c (i) as (select 1 union all select i + 1 from c where i < 1501)
    select count(*) as n from c
end sql

check("unset, it answers the default", bank.max_recursion, 1000000)
check("under the default, 5 000 rounds run", long_series.query()[0].n, 5001)

bank.max_recursion = 2000
check("it reads back as set", bank.max_recursion, 2000)
check("at 2 000, the 5 000-round query is refused", refused(long_series), "EXPRESSION")
check("at 2 000, a 1 500-round query still runs", short_series.query()[0].n, 1501)
' a SQL string on the same connection obeys it too
on error goto next
x = gsql.query(bank, "with recursive c (i) as (select 1 union all select i + 1 from c where i < 3001) select count(*) as n from c")
if error then
    check("the refusal names max_recursion", contains(error.message, "max_recursion"), true)
else
    check("a 3 000-round string at 2 000 is refused", false, true)
end if

' a block's every copy shares it: the connection is one object
same = bank
same.max_recursion = 6000
check("set through a copy, the block sees it", bank.max_recursion, 6000)
check("and the 5 000-round query runs again", long_series.query()[0].n, 5001)

bank.max_recursion = unknown
check("unknown restores the default", bank.max_recursion, 1000000)

' refusals, each naming what the connection takes
bank.max_recursion = 0
if error then
    check("0 is refused, naming whole rounds", contains(error.message, "whole number of rounds"), true)
else
    check("0 is refused", false, true)
end if
bank.max_recursion = 2.5
if error then
    check("2.5 is refused", contains(error.message, "whole number of rounds"), true)
else
    check("2.5 is refused", false, true)
end if
bank.max_recursion = "2000"
if error then
    check("text is refused", contains(error.message, "whole number of rounds"), true)
else
    check("text is refused", false, true)
end if
bank.max_rounds = 5
if error then
    check("another attribute name is refused, naming max_recursion", contains(error.message, "max_recursion"), true)
else
    check("another attribute name is refused", false, true)
end if
y = bank.timeout
if error then
    check("reading another attribute is refused", contains(error.message, "its one attribute is max_recursion"), true)
else
    check("reading another attribute is refused", false, true)
end if
check("a refused setting left it unchanged", bank.max_recursion, 1000000)

bank.close()
print "ok " + string(checks.n) + " checks, " + string(checks.bad) + " mismatches"
