' valid_utf8 and bytes_xor against cases Python decided
' (tests/websocket/bytes_cases.py). Run by tests/run_websocket.sh.
'
'   gbasic bytes_test.bas CASES_FILE
function unhex(h)
    if h = "-" then
        return ""
    end if
    return hex_decode(h)
end function

program main(args)
    f {file}= args[0]
    n = 0
    bad = 0
    for each line in split(trim(read(f)), chr(10))
        p = split(line, " ")
        b = unhex(p[0])
        want = p[1] = "true"
        n = n + 1
        if valid_utf8(b) != want then
            bad = bad + 1
            print "MISMATCH valid_utf8 " + p[0] + ": got " + string(valid_utf8(b)) + ", Python says " + p[1]
        end if
        if bytes_xor(b, unhex(p[2])) != unhex(p[3]) then
            bad = bad + 1
            print "MISMATCH bytes_xor " + p[0] + " key " + p[2]
        end if
    end for
    print "checks: " + string(n * 2)
    print "mismatches: " + string(bad)
end program
