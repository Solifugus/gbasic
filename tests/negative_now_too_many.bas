' `now` takes no arguments, or one zone name. Two is an arity error, and it is
' worth its own case: `now(5)` now reports a TYPE problem (a zone must be a
' string), so nothing else pins the arity rule any more.
print(now("UTC", "Asia/Tokyo"))
