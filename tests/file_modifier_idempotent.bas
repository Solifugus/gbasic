' PLAT: `{file}` and `{dir}` are idempotent.
'
' `{file}` is how one ASSERTS that a path is a file, and asserting it of
' something already a file should be a no-op rather than an error -- the same
' reasoning that makes `{USD}` idempotent on money.
'
' It matters most where the value came from a listing. `list_files` yields
' FILE values, so `f {file}= entry` raised "file modifier expects a path
' string" -- and the error surfaced AT THE MODIFIER, so it read as "the
' modifier is broken" rather than "the listing returned a type you did not
' expect". Reported by the Transward build.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

d {dir}= "stdlib"

' The case that cut the reporter: an element straight out of a listing.
entry = list_files(d)[0]
check("list_files yields file values", type(entry), "file")
f {file}= entry
check("{file} passes a file value through", type(f), "file")
check("and it is the same path", string(f), string(entry))

' Round-tripping must not damage it either.
g {file}= "stdlib/frame.bas"
h {file}= g
check("{file} on a file value is a no-op", string(h), "stdlib/frame.bas")

' {dir} likewise.
e {dir}= d
check("{dir} passes a dir value through", type(e), "directory")
check("and keeps its path", string(e), "stdlib")

' The passthrough must not weaken the refusals: a wrong type is still refused.
on error goto next
bad {file}= 42
check("a number is still refused", error.message, "file modifier expects a path string")
error.clear()
baddir {dir}= [1, 2]
check("an array is still refused by {dir}", error.message, "dir modifier expects a path string")
error.clear()
' And crossing the two is refused -- a directory is not a file.
crossed {file}= d
check("{file} refuses a DIR value", error.message, "file modifier expects a path string")
error.clear()
on error stop

' The control: a plain string still becomes a file, which is the ordinary use.
p {file}= "stdlib/frame.bas"
check("a string still becomes a file", type(p), "file")

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
