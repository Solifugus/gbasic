' The property a printed line could never have: catchable, so a caller can
' tell a failed listing from an empty directory. Both are `nothing`-shaped
' without this.
on error goto next
r = folders(42)
if error then
    print("caught: " + error.message)
    error.clear()
end if
d{dir} = "."
ok = list(d)
print("a real listing still works: " + string(count(ok) >= 0))
