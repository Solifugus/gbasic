' Inside a block the synthetic end-of-file lands where the grammar refuses it,
' so this ALREADY exited nonzero. What it also did was print two lines: an
' unlocated bare one, and then "unexpected end of file" — naming a fiction,
' because the file does not end here and `dim` is what actually stopped it.
program main( args )
    print "before"
    dim x
    print "after"
end program
