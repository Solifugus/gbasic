' An all-day value has no instant: converting a due date through a zone would
' manufacture a midnight and the classic off-by-one-day bug with it.
d {date}= "2026-08-17"
print from_zone(d, "America/New_York")
