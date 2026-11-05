' Whole-file verbs take a path string as well as a `{file}` reference.
'
' Until 2026-11-05 `read`, `write`, `append`, `exists`, `delete`, `read_lines`,
' `bytes`, `lines`, `chars`, `overwrite`, `read_at`, `file_size`, `file_mtime`,
' `lock`, `unlock`, `copy`/`move`'s source and `with lock` refused a string,
' while `join_path`, `list`, `csv.records`, `zip.read` and `xml.parse_file`
' took either. So the obvious line
'
'     read(join_path(directory_name(program_file()), "spec.txt"))
'
' raised, and every program grew the same `{file}` helper.
'
' SELF-CHECKING. Each verb is called with the string and its answer compared
' with the same verb given `{file}` of the same path. The `{file}` route is
' unchanged, so it is the oracle: a string handled any other way than
' `{file}` would handle it gives a different answer here.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print ("ok   " + label)
    else
        tally.mismatches = tally.mismatches + 1
        print ("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
    return nothing
end function

' the message a refused call raises, or "no error"
function refusal(verb, a, b)
    on error goto next
    if verb = "read" then
        t = read(a)
    else if verb = "write" then
        write(a, b)
    else if verb = "exists" then
        t = exists(a)
    else if verb = "delete" then
        delete(a)
    else if verb = "copy" then
        copy(a, b)
    else if verb = "append" then
        append(a, b)
    end if
    if error then
        m = error.message
        error.clear()
        return m
    end if
    return "no error"
end function

scratch = temp_dir()
p = join_path(scratch, "a.txt")
check("join_path answers text, the case this is for", type(p), "string")

print "-- writing and reading through a string"
write(p, "one\ntwo\n")
check("write(string) wrote the file read({file}) sees", read({file}p), "one\ntwo\n")
check("read(string) reads it", read(p), "one\ntwo\n")
append({file}p, "three\n")
check("append({file}inline), which used to be taken for the array verb", read({file}p), "one\ntwo\nthree\n")
check("read_lines(string)", read_lines(p), read_lines({file}p))
check("and it is the lines", read_lines(p), [ "one", "two", "three" ])

print "-- counts and metadata"
check("bytes(string)", bytes(p), bytes({file}p))
check("lines(string)", lines(p), 3)
check("chars(string)", chars(p), chars({file}p))
check("file_size(string)", file_size(p), file_size({file}p))
check("file_mtime(string)", file_mtime(p), file_mtime({file}p))
check("exists(string) of a file", exists(p), true)
check("exists(string) of a folder, as exists({dir}) answers", exists(string(scratch)), exists(scratch))
check("exists(string) of nothing there", exists(join_path(scratch, "absent.txt")), false)

print "-- in place"
overwrite(p, "ONE", 0)
check("overwrite(string)", read({file}p), "ONE\ntwo\nthree\n")
check("read_at(string)", read_at(p, 4, 3), "two")

print "-- copying, moving, deleting"
q = join_path(scratch, "b.txt")
copy(p, q)
check("copy(string, string)", read({file}q), "ONE\ntwo\nthree\n")
r = join_path(scratch, "c.txt")
move(q, r)
check("move(string, string) moved it", [ exists({file}q), read({file}r) ], [ false, "ONE\ntwo\nthree\n" ])
delete(r)
check("delete(string)", exists({file}r), false)

print "-- locks"
lock(p)
unlock(p)
check("lock and unlock take a string", "returned", "returned")
seen = ""
with lock(p)
    seen = read(p)
end with
check("with lock takes a string", seen, "ONE\ntwo\nthree\n")

print "-- append is the exception: it is also the array verb"
check("append(string) is refused, not taken as a path", refusal("append", p, "x"),
      "append expects an array, or a file reference to add text to a file; it does not take a string as a path, so write {file}path")
check("and wrote nothing", read({file}p), "ONE\ntwo\nthree\n")
log = { f: {file}join_path(scratch, "log.txt") }
write(log.f, "a\n")
append(log.f, "b\n")
check("append to a record field holding a {file}", read(log.f), "a\nb\n")
lf{file}= join_path(scratch, "log.txt")
append(lf, "c\n")
check("and to a {file} variable, as before", read(lf), "a\nb\nc\n")
arr = [ 1 ]
append(arr, 2)
check("arrays are untouched", arr, [ 1, 2 ])
delete(lf)

print "-- the book's line"
here = directory_name(program_file())
me = read(join_path(here, "file_path_string_test.bas"))
check("read(join_path(directory_name(program_file()), ...)) reads a neighbour",
      starts_with(me, "' Whole-file verbs take a path string"), true)

print "-- what is still refused, and how it says so"
check("a number is not a path", refusal("read", 42, nothing), "read expects a path string or file reference")
check("nor for exists", refusal("exists", 42, nothing),
      "exists expects a path string, file reference or directory reference")
check("write still wants text to write", refusal("write", p, 42),
      "write expects a path string or file reference, and the text to write")
check("copy names both ends", refusal("copy", 42, q),
      "copy expects a path string or file reference for both source and target")
check("an interior NUL is refused as {file} refuses it",
      refusal("read", "a.txt" + chr(0) + "b", nothing), "read: a path cannot contain an interior NUL byte")
check("by delete too", refusal("delete", "a.txt" + chr(0) + "b", nothing), "delete: a path cannot contain an interior NUL byte")
check("and the folder still holds only a.txt", count(list(scratch)), 1)
check("a missing file through a string says what {file} says",
      refusal("read", join_path(scratch, "absent.txt"), nothing),
      refusal("read", {file}join_path(scratch, "absent.txt"), nothing))

delete(p)
remove_tree(scratch)

print ""
print ("checks: " + string(tally.checks))
print ("mismatches: " + string(tally.mismatches))
