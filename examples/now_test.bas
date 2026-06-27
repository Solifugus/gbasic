' now() exposes the current local time as a second-precision datetime value.
' The exact instant is non-deterministic, so this test asserts only stable
' derived facts: the value kind, that duration arithmetic advances it, and that
' day-precision comparison behaves. (today() is intentionally NOT a builtin --
' `today` is too common an identifier to claim; derive a date with the (day)=
' truncation lens, e.g. `today(day)= now()`, instead.)
n = now()
print(type(n))

' Datetime + duration arithmetic already exists; now() makes it usable for
' real expiration math ("has now() passed a stored deadline?").
later = n + 1 hour
after = later > n
print(after)

tomorrow = n + 1 day
is_after = tomorrow > n
print(is_after)

' A day-precision comparison of a value to itself holds regardless of the clock.
same = n {day}= n
print(same)
