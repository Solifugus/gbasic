' crypto ciphers — AES-GCM authenticated encryption and Ed25519 signatures
' (docs/crypto_design.md Phase 4). AES-256-GCM with a fixed key/nonce is
' deterministic and cross-checked against pyca/cryptography; Ed25519 keys are
' random, so those checks are round-trip booleans plus tamper rejection.
program demo(args)
    ' --- AES-256-GCM, fixed key + nonce => deterministic ciphertext||tag ---
    key = hex_decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
    nonce = hex_decode("101112131415161718191a1b")
    pt = "attack at dawn"
    aad = "hdr"
    blob = aes_gcm_encrypt(key, nonce, pt, aad)
    print("aes_ct " + hex_encode(blob))
    print("aes_rt " + aes_gcm_decrypt(key, nonce, blob, aad))

    ' tamper / wrong-aad / bad-key-length all reject
    ' Bare `+ 1` is safe HERE and only here: key and nonce are fixed, so this
    ' blob is deterministic and its first byte is 28. The identical-looking
    ' line in the Ed25519 block below is NOT safe, because a signature is
    ' random -- see the note there before making the two match.
    tampered = from_bytes([byte_at(blob, 0) + 1]) + mid(blob, 1, byte_count(blob) - 1)
    print("aes_tamper " + string(aes_gcm_decrypt(key, nonce, tampered, aad)))
    print("aes_badaad " + string(aes_gcm_decrypt(key, nonce, blob, "wrong")))
    print("aes_badkey " + string(aes_gcm_encrypt("short", nonce, pt, aad)))
    print("aes_badnonce " + string(aes_gcm_encrypt(key, "bad", pt, aad)))

    ' empty plaintext is valid (tag-only blob, 16 bytes)
    eblob = aes_gcm_encrypt(key, nonce, "", "")
    print("aes_empty_len " + string(byte_count(eblob)) + " rt [" + aes_gcm_decrypt(key, nonce, eblob, "") + "]")

    ' AES-128 with a 16-byte key
    k16 = hex_decode("000102030405060708090a0b0c0d0e0f")
    b16 = aes_gcm_encrypt(k16, nonce, pt, aad)
    print("aes128_rt " + aes_gcm_decrypt(k16, nonce, b16, aad))

    ' --- Ed25519 round-trip (keys are random) ---
    kp = ed25519_keypair()
    print("ed_keylen " + string(byte_count(kp.public)) + " " + string(byte_count(kp.private)))
    msg = "sign me"
    sig = ed25519_sign(kp.private, msg)
    print("ed_siglen " + string(byte_count(sig)))
    print("ed_ok " + string(ed25519_verify(kp.public, msg, sig)))
    print("ed_badmsg " + string(ed25519_verify(kp.public, "tampered", sig)))
    ' `mod` and not `+ 1`: an Ed25519 signature is random, so its first byte is
    ' 255 about once every 256 runs, and from_bytes refuses 256 -- a test that
    ' fails 0.4% of the time is worse than one that always fails, because it
    ' teaches you to re-run instead of to look.
    badsig = from_bytes([mod(byte_at(sig, 0) + 1, 256)]) + mid(sig, 1, byte_count(sig) - 1)
    print("ed_badsig " + string(ed25519_verify(kp.public, msg, badsig)))
    ' wrong key length rejected
    print("ed_badkey " + string(ed25519_sign("short", msg)))
end program
