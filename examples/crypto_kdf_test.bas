' Password-based key derivation (docs/crypto_design.md Phase 5).
'
' The gap this closes: `crypto` had sha256/hmac/aes-gcm but no KDF, so there was
' no safe way to turn a PASSPHRASE into a key. gBASIC Studio declined to offer
' passphrase-protected secrets rather than ship a single-round hash that looks
' like one (DOGFOOD 2026-08-22). `password_hash` is a different job -- it
' verifies a login and returns a hash STRING; these return raw key BYTES.
'
' Every vector below is checked against an INDEPENDENT implementation, never
' against gBASIC itself: PBKDF2 against python3 hashlib.pbkdf2_hmac, scrypt
' against RFC 7914 §12, which python3 hashlib.scrypt also reproduces. A KDF that
' agrees only with itself proves nothing at all -- if it and its verifier share a
' bug the round trip still succeeds, and the derived key is simply weak.
program main(args)
    ' --- PBKDF2-HMAC-SHA256, low iteration counts so the vectors stay checkable
    print "pbkdf2_sha256 i=1     " + hex_encode(pbkdf2_sha256("password", "salt", 1, 32))
    print "pbkdf2_sha256 i=4096  " + hex_encode(pbkdf2_sha256("password", "salt", 4096, 32))
    print "pbkdf2_sha512 i=4096  " + hex_encode(pbkdf2_sha512("password", "salt", 4096, 64))

    ' Length is honoured exactly, and a longer derivation extends the shorter one
    ' (PBKDF2 is defined block-wise), which is a property worth pinning: it is
    ' what lets a caller derive a key and an IV from one passphrase in one call.
    short = pbkdf2_sha256("password", "salt", 4096, 16)
    long = pbkdf2_sha256("password", "salt", 4096, 32)
    print "pbkdf2 prefix         " + string(hex_encode(short) = mid(hex_encode(long), 0, 32))
    print "pbkdf2 length 1       " + string(byte_count(pbkdf2_sha256("p", "s", 2, 1)))
    print "pbkdf2 length 64      " + string(byte_count(pbkdf2_sha256("p", "s", 2, 64)))

    ' --- scrypt, RFC 7914 §12 vectors 2 and 3 -----------------------------
    ' Vector 1 (empty password AND empty salt) is deliberately unreachable: an
    ' empty salt is refused, because it silently turns a KDF into a plain
    ' iterated hash. Vector 4 needs 1 GB and exceeds the memory ceiling by
    ' design -- see the negative fixtures.
    print "scrypt N=1024         " + hex_encode(scrypt("password", "NaCl", 1024, 8, 16, 64))
    print "scrypt N=16384        " + hex_encode(scrypt("pleaseletmein", "SodiumChloride", 16384, 8, 1, 64))

    ' --- the point of the exercise: a passphrase becomes an AES-GCM key ----
    salt = "a-stored-random-salt"
    key = pbkdf2_sha256("correct horse battery staple", salt, 100000, 32)
    print "derived key bytes     " + string(byte_count(key))
    nonce = hex_decode("101112131415161718191a1b")
    blob = aes_gcm_encrypt(key, nonce, "the launch code is 0000", "")
    same = pbkdf2_sha256("correct horse battery staple", salt, 100000, 32)
    print "same passphrase       " + aes_gcm_decrypt(same, nonce, blob, "")
    ' A different passphrase derives a different key, so the tag fails and the
    ' plaintext is never returned -- `unknown`, not garbage.
    other = pbkdf2_sha256("correct horse battery stapler", salt, 100000, 32)
    print "wrong passphrase      " + string(aes_gcm_decrypt(other, nonce, blob, ""))
    ' ...and so does the same passphrase with a different salt, which is what
    ' makes the salt worth storing beside the ciphertext.
    resalted = pbkdf2_sha256("correct horse battery staple", "another-salt", 100000, 32)
    print "same pass, new salt   " + string(aes_gcm_decrypt(resalted, nonce, blob, ""))
end program
