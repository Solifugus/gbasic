' Writes each line of a JSON-lines file (an array of fields per line) as one
' record with csv.line, for roundtrip.py to read back with Python's reader.
program main(args)
    delims = { comma: ",", tab: chr(9), pipe: "|", semi: ";" }
    input{file}= args[0]
    output{file}= args[1]
    opts = { delimiter: delims[args[2]] }
    if args[3] = "crlf" then
        opts.line_end = chr(13) + chr(10)
    else
        opts.line_end = chr(10)
    end if
    write(output, "")
    for each entry in input
        append(output, csv.line(decode(entry), opts))
    end for
end program
