name{lowered}= "Joe Jones"
print(name)

label{uppered}= "abc"
print(label)

command{trimmed}= "  LOOK  "
command{lowered}= command
print(command)

' Both spellings are the same modifier (2026-08-18): the builtin's name and
' the participle. The near-miss used to raise `assign modifier not found`.
a{upper}= "both spellings"
print(a)
b{lower}= "WORK NOW"
print(b)
c{trim}= "  and trim  "
print(c)
