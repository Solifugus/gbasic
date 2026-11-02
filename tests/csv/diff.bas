' Reads every case gen_csv_diff.py wrote and prints each record as the
' generator's `expected` does: case, line, offset, fields as JSON.
program main(args)
    delims = { comma: ",", tab: chr(9), pipe: "|", semi: ";" }
    quotes = { dq: "\"", sq: "'", none: "" }
    manifest{file}= args[0]
    for each entry in manifest
        part = split(entry, chr(9))
        source{file}= part[1]
        opts = { header: false, delimiter: delims[part[2]], quote: quotes[part[3]] }
        for each r, at in csv.records(source, opts)
            print(part[0] + " " + string(at.line) + " " + string(at.offset) + " " + json_encode(r))
        end for
    end for
end program
