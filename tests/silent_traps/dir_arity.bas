' A THIRD ARGUMENT IS STILL AN ARITY ERROR. This fixture used to pass TWO
' arguments, which was the refusal when `list` took a directory and nothing
' else; the second argument is the glob pattern now (2026-10-15), so the
' deliberate mistake moved out by one. What is being asserted has not moved:
' a call this family cannot make is a LOCATED raise and not a bare line on
' stderr followed by `nothing` and exit 0.
d{dir} = "."
r = list(d, "*", "extra")
print("unreachable")
