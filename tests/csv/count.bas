' Streams a file and counts its records: memory should be one record's.
program main(args)
    f{file}= args[0]
    n = 0
    for each item in csv.records(f)
        n = n + 1
    end for
    print(n)
end program
