' Program arguments: a `program` block's declared parameter binds to the list of
' command-line arguments that follow the script path. The list is a 0-based array
' of strings; with no trailing arguments it is empty (count 0), never unbound.
program main(args)
    print("argc: " + string(count(args)))
    if count(args) > 0 then
        print("first: " + args[0])
    end if
    for each a in args
        print("arg: " + a)
    end for
end program
