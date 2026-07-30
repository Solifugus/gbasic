' PLAT-CLAUSE residual, pinned deliberately.
'
' An UNQUALIFIED call to a function from a LOADED LIBRARY, with a single
' argument, followed by a comparison. Neither option A nor option F reaches it:
' the preceding token is an ordinary identifier, and there is no dot. The
' lookahead's function check cannot see across the file boundary, so `(1)` is
' still read as a modifier clause -- and because `1` is a legal clause body, it
' PARSES, and fails at run time complaining about a modifier named `1`.
'
' This test asserts what it does, not what it should do. If clause recognition
' is ever narrowed further -- requiring a clause body to begin with an
' identifier would do it, since a modifier name is always identifier-like --
' this test fails and must be retired along with the residual.
program main(args)
  load clause_probe from "../examples/libs/clause_probe.bas"
  if kind(1) = "record" then
    print "unreachable"
  end if
end program
