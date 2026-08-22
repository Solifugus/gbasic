' has_builtin: the feature probe. A gBASIC program runs on whatever interpreter
' is installed, and nothing marks which builtins postdate a release — this asks
' at run time, so code can degrade instead of crashing on an older build.

program main(args)
  print "-- names from the parser registry"
  print string(has_builtin("file_type"))
  print string(has_builtin("env"))
  print string(has_builtin("sha256"))
  print string(has_builtin("has_builtin"))

  ' Every dispatch-only name, pinned deliberately: these are dispatched inside
  ' eval.c without being registered, and the probe knows them through a second
  ' list in builtins.c that nothing else keeps in step. Removing one from that
  ' list turns this test red.
  print "-- the dispatch-only names (file and directory calls)"
  for each n in ["exists", "read", "write", "bytes", "lines", "chars", "lock", "unlock", "list", "files", "folders"]
    if not has_builtin(n) then
      print "MISSING: " + n
    end if
  end for
  print "all present"

  print "-- module-scoped names are not unqualified builtins"
  print string(has_builtin("run"))
  print string(has_builtin("which"))
  print string(has_builtin("listen"))

  print "-- an unknown name"
  print string(has_builtin("definitely_not_a_builtin"))

  print "-- the pattern this exists for"
  if has_builtin("file_type") then
    print "new interpreter: precise checks available"
  else
    print "released interpreter: fallback path"
  end if
end program
