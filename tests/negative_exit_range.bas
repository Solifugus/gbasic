' The kernel keeps only the low byte, so a truncated 256 would report SUCCESS
' from a program that meant to fail. Refused instead.
exit(256)
