' process.start's `limits: { memory: n }` (gdash's GDASH-16 B1). Arguments: a
' scratch directory and the gbasic to run; the runner reads what the CHILD reports of
' its own limits from /proc, which is the kernel's answer, not gBASIC's.
program main( args )
    dir = args[0]
    gb = args[1]
    ' 1. the limit the child runs under, as the kernel shows it to the child
    h = process.start({ command: "cat", args: ["/proc/self/limits"], limits: { memory: 150000000 } })
    s = process.wait(h)
    out = process.read(h)
    for each line in split(out.stdout, "\n")
        if starts_with(line, "Max data size") or starts_with(line, "Max core file size") then
            print "limits " + line
        end if
    end for
    print "limited status " + string(s.memory_limit) + " " + string(s.peak_memory > 0)
    ' 2. a gBASIC child that outgrows it, and the same child under a roomy limit
    grow = dir + "/grow.bas"
    write(grow, "a = []\nfor i = 1 to 3000000\n  a = append(a, \"row \" + string(i) + \" padded padded padded padded\")\nnext\nprint len(a)\n")
    h = process.start({ command: gb, args: [grow], cwd: dir, limits: { memory: 100000000 } })
    s = process.wait(h)
    print "tight " + string(s.success) + " " + string(s.signal) + " " + string(s.peak_memory >= 50000000)
    h = process.start({ command: gb, args: [grow], limits: { memory: 4000000000 } })
    s = process.wait(h)
    print "roomy " + string(s.success) + " " + trim(process.read(h).stdout)
    ' 3. no limit, no new fields: the status a program already prints is unchanged
    h = process.start({ command: "true", args: [] })
    print "plain " + string(process.wait(h))
    ' 4. refusals, by name
    r = try process.start({ command: "true", args: [], limits: { memroy: 5 } })
    print "refused " + error.message
    r = try process.start({ command: "true", args: [], limits: { memory: 0 } })
    print "refused " + error.message
    r = try process.start({ command: "true", args: [], limits: 5 })
    print "refused " + error.message
end program
