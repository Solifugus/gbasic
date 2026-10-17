' A NUMBER is refused by name, with the arithmetic in the message: six hundred
' is a perfectly good mode_t and would set bits nobody asked for AND SUCCEED.
' The path cannot exist, deliberately -- the mode is validated before anything
' touches the filesystem, so this test can never alter a real file.
print chmod({file}"/nonexistent-gbasic-probe", 0600)
