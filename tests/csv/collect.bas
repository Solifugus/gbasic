' The control for count.bas: keeps every record, so memory grows with the file.
program main(args)
    f{file}= args[0]
    all = []
    for each item in csv.records(f)
        append(all, item)
    end for
    print(count(all))
end program
