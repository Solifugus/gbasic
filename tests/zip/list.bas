' Every member of every archive named on the command line, in the line form
' tests/make_zip_cases.py writes from Python's zipfile:
' archive|name|size|crc|method|folder|hex of the bytes. Run by tests/run_zip.sh.
program main(args)
    for each a in args
        for each m in zip.members({file}a)
            data = ""
            if not m.folder then
                data = hex_encode(zip.read({file}a, m.name))
            end if
            print (file_name(a) + "|" + m.name + "|" + string(m.size) + "|" + string(m.crc) + "|" + m.method + "|" +
                   string(m.folder) + "|" + data)
        end for
    end for
end program
