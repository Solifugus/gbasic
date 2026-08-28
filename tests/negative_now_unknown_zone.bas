' A zone name that is not in the IANA database is refused rather than guessed
' or silently treated as UTC -- the whole point of the zone argument is that a
' wrong answer here is invisible.
print(now("Mars/Olympus"))
