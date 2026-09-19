' `round` -- places is OPTIONAL. SELF-CHECKING rather than a golden, because
' every defect here is a PLAUSIBLE NUMBER: a default of 1 place, a truncation
' instead of a rounding, or half-to-even instead of half-away-from-zero each
' produce a number a reader would accept, and a golden would record it as
' expected and defend it.
'
' The one-argument form was REFUSED until 2026-09-19 -- the form every other
' BASIC has, and the first thing a reader tries. Its neighbours `floor`,
' `ceil`, `abs` and `sqrt` all take one argument, so nothing chose to make the
' commonest call of the family the one that fails.

function check(label, got, want)
  if string(got) = string(want) then
    print "ok   " + label
  else
    print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
  end if
end function

' One argument: to the nearest whole number.
check("round up", round(3.7), 4)
check("round down", round(3.2), 3)
check("negative rounds away from zero", round(-3.7), -4)
check("a half rounds away from zero", round(2.5), 3)
check("and so does a negative half", round(-2.5), -3)
check("a whole number is itself", round(5), 5)

' THE CONTROL that the default is 0 rather than some other number of places:
' a value with decimals must come back with NONE. Without this, a default of
' 1 or 2 places passes every check above except the halves.
check("the default is zero places", round(3.14159), 3)

' THE OTHER CONTROL: the two-argument form is unchanged. A change that made
' places optional by IGNORING it would pass everything above.
check("two arguments still round to places", round(3.14159, 2), 3.14)
check("and to more places", round(3.14159, 4), 3.1416)
check("zero places asked for explicitly", round(3.7, 0), 4)

' Both forms must agree where they overlap, which is the relationship the
' default IS -- asserted rather than assumed, since two code paths compute it.
check("the forms agree", round(3.7), round(3.7, 0))
check("the forms agree on a negative", round(-8.5), round(-8.5, 0))
