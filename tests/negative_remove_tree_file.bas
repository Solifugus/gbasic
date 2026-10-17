' A FILE is not a tree: the message names `delete` rather than refusing blankly.
' The file is this test itself, so it exists on every platform (it named
' /etc/hostname, which Windows does not have -- there the case answered "No such
' file or directory" and never reached the refusal it exists to pin).
print remove_tree({file}"tests/negative_remove_tree_file.bas")
