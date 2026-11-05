' transcode, valid_utf8 and first_invalid_utf8 against cases Python's codecs
' decided (tests/text/encoding_cases.py), then the cases the oracle does not
' sample. Self-checking; run by tests/run_text_encoding.sh.
'
'   gbasic encoding_test.bas CASES_FILE
function unhex(h)
    if h = "-" then
        return ""
    end if
    return hex_decode(h)
end function

' The result as the oracle writes it: hex, or ERR:<offset or character>.
function attempt(data, from_to)
    on error goto refused
    return hex_encode(transcode(data, from_to[0], from_to[1]))
refused:
    at = "?"
    if has(error.details, "offset") then
        at = string(error.details.offset)
    else if has(error.details, "character") then
        at = string(error.details.character)
    end if
    error.clear()
    return "ERR:" + at
end function

function message_of(data, from_to)
    on error goto refused
    r = transcode(data, from_to[0], from_to[1])
    return "ANSWERED"
refused:
    m = error.message
    error.clear()
    return m
end function

function check(label, got, want)
    G.checks = G.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        G.mismatches = G.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

program main(args)
    G = { checks: 0, mismatches: 0 }
    f {file}= args[0]
    n = 0
    bad = 0
    for each line in split(trim(read(f)), chr(10))
        p = split(line, " ")
        data = unhex(p[0])
        want = p[3]
        if want = "-" then
            want = ""
        end if
        got = attempt(data, [p[1], p[2]])
        n = n + 1
        if got != want then
            bad = bad + 1
            if bad < 20 then
                print "MISMATCH " + p[1] + " -> " + p[2] + " of " + p[0] + ": got " + got + ", Python says " + p[3]
            end if
        end if
        if p[1] = "utf-8" then
            ok = not starts_with(p[3], "ERR") or p[2] != "utf-8"
            first = first_invalid_utf8(data)
            if starts_with(p[3], "ERR") and p[2] = "utf-8" then
                if string(first) != mid(p[3], 4, len(p[3]) - 4) or valid_utf8(data) then
                    bad = bad + 1
                    print "MISMATCH first_invalid_utf8 " + p[0] + ": got " + string(first) + ", Python says " + p[3]
                end if
            else if p[2] = "utf-8" and (not is_nothing(first) or not valid_utf8(data)) then
                bad = bad + 1
                print "MISMATCH valid_utf8 " + p[0] + " is valid to Python"
            end if
        end if
    end for
    G.checks = n
    G.mismatches = bad
    print "oracle cases: " + string(n)

    print "-- WHAT THE ORACLE DOES NOT SAMPLE --"
    be = transcode("Aé€", "utf-8", "utf-16be")
    check("utf-16 with no byte-order mark reads big-endian (RFC 2781)", transcode(be, "utf-16", "utf-8"), "Aé€")
    check("utf-16 with FE FF reads big-endian", transcode(from_bytes([254, 255]) + be, "utf-16", "utf-8"), "Aé€")
    check("utf-16 written is FF FE then little-endian", hex_encode(transcode("A", "utf-8", "utf-16")), "fffe4100")
    check("names ignore case, hyphens and underscores", transcode("é", "UTF_8", "Latin1"), from_bytes([233]))
    check("an empty string is empty in every encoding", transcode("", "utf-16", "windows-1252"), "")
    check("the book's case: Latin-1 Câble", transcode("C" + from_bytes([226]) + "ble", "latin-1", "utf-8"), "Câble")
    check("a decode refusal names the byte",
          message_of(from_bytes([67, 226, 98]), ["utf-8", "utf-8"]), "transcode: byte 1 is not valid UTF-8")
    check("an encode refusal names the character and the encoding",
          message_of("a中", ["utf-8", "latin-1"]), "transcode: character 1 (U+4E2D) has no byte in latin-1")
    check("an unknown encoding lists the known ones", starts_with(message_of("a", ["utf-8", "ebcdic"]),
          "transcode: unknown encoding 'ebcdic' (known: utf-8, latin-1"), true)
    check("first_invalid_utf8 of valid text is nothing", is_nothing(first_invalid_utf8("héllo")), true)
    check("  and of a bad byte its offset", first_invalid_utf8("ab" + from_bytes([255])), 2)
    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.mismatches)
end program
