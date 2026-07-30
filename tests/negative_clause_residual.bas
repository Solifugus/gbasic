' PLAT-CLAUSE residual, pinned deliberately -- and REDUCED by PLAT-CLAUSE-B.
'
' An UNQUALIFIED call to a function from a LOADED LIBRARY, compared with `=`.
' Option A permits it (the preceding token is an ordinary identifier), option F
' does not apply (there is no dot), and the lookahead's function check cannot see
' across a file boundary.
'
' PLAT-CLAUSE-B closed the numeric and string argument forms -- `kind(1)` and
' `kind("q")` -- because a clause body always begins with a modifier NAME, and a
' name is always identifier-shaped.
'
' What is left is the IDENTIFIER argument, and it is not closable at token
' delivery. `name(caseless) = "joe"` and `kind(x) = "record"` are the same tokens
' in the same order -- IDENT ( IDENT ) = STRING -- and the first must be a clause
' while the second must be a call. Telling them apart requires knowing whether
' `caseless` is a registered modifier or `kind` is callable, and neither fact
' exists until eval. This is the argument for option D, which is deferred.
'
' The failure is a RUN-TIME error naming the argument, because `x` is a legal
' clause body so the program parses. Asserted as what it does, not as fixed.
program main(args)
  load clause_probe from "../examples/libs/clause_probe.bas"
  x = 1
  if kind(x) = "record" then
    print "unreachable"
  end if
end program
