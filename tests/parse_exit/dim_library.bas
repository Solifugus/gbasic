' The library path already refused to load — it is the DIAGNOSTIC that was
' wrong: three lines for one cause, one of them unlocated and one of them
' blaming end-of-file.
program main( args )
    load broken from "lib/broken.bas"
    print broken.alpha(1)
end program
