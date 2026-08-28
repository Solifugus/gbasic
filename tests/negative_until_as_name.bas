' `until` became reserved on 2026-08-27 when `do ... until c` dropped the
' `loop` in front of it: the word is now statement-initial, so it cannot also
' be a variable. `loop` went the other way and is an ordinary name again.
until = 3
print(until)
