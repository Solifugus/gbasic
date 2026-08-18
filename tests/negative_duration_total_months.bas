' total_seconds on a month-bearing duration is refused: a month has no fixed
' length, so any number here would be a guess dressed as a fact.
t = 1 month 3 days
print t.total_seconds
