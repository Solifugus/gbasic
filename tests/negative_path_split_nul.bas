' The door the same function left open for four of its six names until 0.9.1:
' `real_path` and `file_type` refused an interior NUL and `file_name` answered
' "b" for "/a/b\0x/c.txt" -- the basename of a path the program was not holding.
print file_name("/a/b" + chr(0) + "x/c.txt")
