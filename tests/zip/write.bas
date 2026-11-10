' zip.write (gap G55): each archive made from the members
' tests/make_zip_write_cases.py chose, for tests/run_zip.sh to read back with
' Python's zipfile. args: the cases' directory, then the case numbers.
program main(args)
    dir = args[0]
    for each k, i in args
        if i = 0 then
            continue
        end if
        members = []
        for each line in split(trim(read({file}join_path(dir, "case_" + k + ".spec"))), chr(10))
            parts = split(line, chr(9))
            if ends_with(parts[1], "/") then
                append(members, { name: parts[1] })
            else
                append(members, { name: parts[1], data: read({file}join_path(join_path(dir, "case_" + k), "m_" + parts[0])) })
            end if
        end for
        zip.write({file}join_path(dir, "case_" + k + ".zip"), members)
    end for
end program
