' `epoch(dt)` places a DATETIME on the timeline; anything else is refused
' rather than coerced, so a string that looks like a date cannot silently
' become an instant computed from a different reading.
print(epoch("2026-01-01"))
