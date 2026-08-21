' real_path and file_type -- the two filesystem questions gBASIC could not ask.
'
' `real_path` resolves `.`, `..` and symlinks through the kernel and reports a
' missing path as `unknown`. `file_type` says "file", "folder" or "other", also
' `unknown` when nothing is there -- the only non-raising way to ask whether a
' path is a directory (`file_size` on one raises, and a raise cannot be caught).
'
' Every check states its own expected answer, and the output names no absolute
' path, so the golden is the same on any machine.

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' `exists` takes a FILE reference, so it cannot answer this; `file_type` can,
' which is the hole the builtin fills.
home (dir)= "examples/tmp_pathprobe"
if file_type("examples/tmp_pathprobe") = "folder" then
    remove_dir(home)
end if
make_dir(home)
inner (dir)= "examples/tmp_pathprobe/sub"
make_dir(inner)
note (file)= "examples/tmp_pathprobe/note.txt"
write(note, "hi")

' --- file_type ------------------------------------------------------------
append(results, check("a regular file is a file", "file", file_type("examples/tmp_pathprobe/note.txt")))
append(results, check("a directory is a folder", "folder", file_type("examples/tmp_pathprobe/sub")))
append(results, check("a missing path is unknown", true, is_unknown(file_type("examples/tmp_pathprobe/nope"))))
append(results, check("and a device is neither", "other", file_type("/dev/null")))

' --- real_path ------------------------------------------------------------
direct = real_path("examples/tmp_pathprobe/note.txt")
roundabout = real_path("examples/tmp_pathprobe/sub/../note.txt")
append(results, check("a .. in the middle resolves away", direct, roundabout))

dotted = real_path("examples/./tmp_pathprobe/./note.txt")
append(results, check("and so does a .", direct, dotted))

append(results, check("the answer is absolute", true, starts_with(direct, "/")))
append(results, check("it ends at the file it names", "note.txt", file_name(direct)))
append(results, check("a missing path is unknown", true, is_unknown(real_path("examples/tmp_pathprobe/nope"))))
append(results, check("and so is a path under a missing directory", true, is_unknown(real_path("examples/tmp_pathprobe/nope/deeper.txt"))))

' A path that leaves a directory and comes back is the same place. This is the
' property a containment check depends on.
loop_back = real_path("examples/tmp_pathprobe/sub/../sub")
append(results, check("out and back is where you started", real_path("examples/tmp_pathprobe/sub"), loop_back))

delete(note)
remove_dir(inner)
remove_dir(home)
append(results, check("the probe cleaned up after itself", true, is_unknown(file_type("examples/tmp_pathprobe"))))

bad = 0
for each verdict in results
    if not verdict then
        bad = bad + 1
    end if
end for
print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
