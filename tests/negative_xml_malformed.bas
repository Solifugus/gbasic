' Malformed XML must surface as a structured gBASIC error carrying libxml2's
' message and position.
'
' The input is deliberately `<a></b>` (a tag MISMATCH) rather than the
' `<root><unclosed></root>` it used to be. That older input makes libxml2 record
' TWO errors, and gBASIC reports the LAST one (xmlCtxtGetLastError) -- so which
' message surfaced depended on the libxml2 build: 2.9.14 (Ubuntu 24.04 LTS, and
' the riscv64 VM) said "Premature end of data in tag root line 1" while 2.15.2
' said "Opening and ending tag mismatch". The golden pinned the latter, so this
' case failed on the current LTS for a reason that was never about gBASIC.
' A single-error input reports identically on both.
program main(args)
    load xml
    doc = xml.parse("<root><a></b></root>")
end program
