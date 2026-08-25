' PLAT-OUTLINE performance / repeated-call / memory driver. Reads a source file
' (args[0]) and runs source_outline over it args[1] times (default 1), printing a
' compact deterministic summary. Used by run_outline.sh for large-file timing,
' repeated-parse stability, and valgrind memory checks. No path is printed.

program main(args)
  path = args[0]
  reps = 1
  if count(args) > 1 then
    reps = number(args[1])
  end if
  ref{file} = path
  src = read(ref)
  nodes = 0
  diags = 0
  ok = false
  i = 0
  while i < reps
    o = source_outline(src)
    ok = o.ok
    nodes = count(o.nodes)
    diags = count(o.diagnostics)
    i = i + 1
  end while
  print "ok=" + ok + " nodes=" + nodes + " diagnostics=" + diags + " reps=" + reps
end program
