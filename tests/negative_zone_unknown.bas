' An unknown zone is REFUSED. This matters more than it looks: glibc's tzset
' falls back to UTC silently on a bad TZ, so a typo would otherwise become
' quietly-UTC arithmetic -- a plausible wrong answer in every output.
d (date)= "2026-08-17 12:00:00"
print to_zone(d, "America/Chigaco")
