' PLAT-STDERR: the byte-level edge cases.
'
' Emits a precisely counted sequence to stderr so the driver can assert the exact
' byte total rather than eyeballing a transcript:
'
'   ""                     -> 1 byte  (the newline print always terminates with)
'   "a"                    -> 2 bytes
'   "x\ny"                 -> 4 bytes (an embedded newline is content, not a
'                                      terminator, and is not doubled)
'   from_bytes([0])        -> 2 bytes (an interior NUL survives; nothing truncates
'                                      at it, because the string is length-carrying
'                                      rather than NUL-terminated)
'   "é"                    -> 3 bytes (2 for the codepoint + newline: bytes are
'                                      passed through, not re-encoded)
'
' Total: 12 bytes, all on fd 2, with fd 1 left completely empty -- the empty-stdout
' half is the point, since a stream that merely COPIED to stderr would still leave
' bytes on stdout.
print to error ""
print to error "a"
print to error "x" + "\n" + "y"
print to error from_bytes([0])
print to error "é"
