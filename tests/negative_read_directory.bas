' `read` on a DIRECTORY. Until 0.9.1 this aborted the interpreter: on Linux
' `fopen` on a directory succeeds, `ftell` answers LONG_MAX, and the allocation
' of that fails, so the process died with SIGABRT and a core dump -- which takes
' block-buffered stdout with it, so a program tracing its own progress printed
' nothing at all. `file_size` on the same path has always named the problem.
print read({file}"stdlib")
