' ENCRYPTION AT REST OVER FIXED-SIZE CHUNKS, the pattern Transward asked for
' (§4): a file built by APPENDING sealed 64 KiB chunks and fsync'd, then read
' back one chunk at a time with read_at at its byte offset and opened. The
' chunk is 64 KiB of plaintext; sealed it is 16 bytes longer (GCM's tag), so
' chunk i starts at i * 65552 and the last one is shorter.
'
' args: [work directory]; needs libcrypto (the runner skips without it)

function check(what, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + what
    else
        G.bad = G.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function refusal(f)
    on error goto handle
    r = fsync(f)
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

function nonce(i)
    return pack(">I I I", [0, 0, i])
end function

program main(args)
    G = { checks: 0, bad: 0 }
    dir = args[0]
    path = join_path(dir, "vault.bin")
    key = random_bytes(32)
    ' five full chunks and a short sixth
    plain = repeat(random_bytes(4096), 80) + hex_decode("00ff00ff")
    size = 65536
    write(path, "")
    at = 0
    i = 0
    while at < byte_count(plain)
        n = size
        if at + n > byte_count(plain) then
            n = byte_count(plain) - at
        end if
        append({file}path, aes_gcm_encrypt(key, nonce(i), byte_slice(plain, at, n), ""))
        at = at + n
        i = i + 1
    end while
    check("fsync answers true once the file is on stable storage", fsync(path), true)
    check("and a directory's entries can be synced", fsync(dir), true)
    check("six chunks, the last short", i, 6)
    check("the file is the plaintext and a 16-byte tag per chunk",
          bytes({file}path), byte_count(plain) + 16 * i)

    print "-- each chunk read back at its offset and opened"
    back = []
    for c = 0 to i - 1
        sealed = read_at({file}path, c * (size + 16), size + 16)
        append(back, aes_gcm_decrypt(key, nonce(c), sealed, ""))
    end for
    check("the chunks reassemble the plaintext byte for byte", join(back, "") = plain, true)
    check("the last chunk is the 4 bytes past five full ones", byte_count(back[5]), 4)
    check("chunk 3 alone, read in the middle", aes_gcm_decrypt(key, nonce(3),
          read_at({file}path, 3 * (size + 16), size + 16), "") = byte_slice(plain, 3 * size, size), true)

    print "-- refusals"
    check("a missing file raises rather than answering",
          refusal(join_path(dir, "no-such-file")),
          "fsync could not sync " + join_path(dir, "no-such-file") + " (No such file or directory)")
    check("a number is not a file", refusal(5), "fsync expects a file reference or a path string")
    delete(path)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.bad)
end program
