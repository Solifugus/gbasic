' disk_space(path) (Transward §11.1): the filesystem holding a path, in exact
' bytes. Prints each answer for tests/run_disk_space.sh to hold against
' Python's os.statvfs and df, and checks what needs no oracle itself.
' args: the paths to ask about, then "--" and a path that is not there.
program main(args)
    missing = args[count(args) - 1]
    for each p in args
        if p = "--" then
            break
        end if
        d = disk_space(p)
        print p + "|" + string(d.total) + "|" + string(d.free) + "|" + string(d.available)
        print "  ordered " + string(d.available <= d.free and d.free <= d.total and d.total > 0)
        ' exact integers, as file_size is: serialize records the kind, which a
        ' double holding the same whole number would not share
        print "  exact " + string(serialize(d.total) = serialize(number(string(d.total))) and serialize(d.available) = serialize(number(string(d.available))))
    end for
    ' a file reference, and a folder reference, as the path builtins take
    print "file ref " + string(disk_space({file}args[0]).total = disk_space(args[0]).total)
    print "dir ref " + string(disk_space({dir}directory_name(args[0])).total = disk_space(args[0]).total)
    on error goto next
    x = disk_space(missing)
    print "missing: " + error.message
    error.clear()
    x = disk_space("a" + chr(0) + "b")
    print "nul: " + string(contains(error.message, "NUL"))
    error.clear()
    x = disk_space(42)
    print "number: " + error.message
    error.clear()
    x = disk_space()
    print "none: " + error.message
    error.clear()
    print "has_builtin " + string(has_builtin("disk_space"))
end program
