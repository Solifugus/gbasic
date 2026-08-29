' PLAT-WARN: a `load` outside the program block never runs.
'
' `load` is an EXECUTABLE statement, and the top-level statements in a file
' with a program block are not walked -- so this import silently does nothing.
' It is documented, and it is still the most confusing way to lose an import:
' for a native module the symptom is at least "library not loaded: xml", but
' for a .bas library it is `invalid function call: shadowlib.start_server`,
' which sends the reader to look inside a library that was never loaded.
load shadowlib

program main(args)
    print "the load above never ran"
end program
