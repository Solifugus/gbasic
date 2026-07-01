' crypto.bas Phase 3 — ergonomic compositions (docs/crypto_design.md §5).
' Hex digests, signed cookies, CSRF tokens, flat-JSON, JWT/HS256, and
' nonce-managing symmetric encrypt/decrypt. The JWT token is byte-for-byte
' identical to PyJWT's HS256 output for the same payload and secret.
program demo(args)
    load crypto from "../stdlib/crypto.bas"
    secret = "topsecret"

    print("sha256_hex " + sha256_hex("hello world"))

    ' --- signed cookies ---
    c = sign_cookie("user=42", secret)
    print("cookie " + c)
    print("cookie_rt " + verify_cookie(c, secret))
    print("cookie_badsecret " + string(verify_cookie(c, "wrong")))
    print("cookie_tamper " + string(verify_cookie(c + "x", secret)))
    print("cookie_malformed " + string(verify_cookie("nodot", secret)))

    ' --- CSRF ---
    t = csrf_token(secret, "sess-abc")
    print("csrf " + t)
    print("csrf_ok " + string(csrf_check(t, secret, "sess-abc")))
    print("csrf_badsession " + string(csrf_check(t, secret, "sess-xyz")))

    ' --- flat JSON round trip ---
    j = json_encode({ sub: "1234567890", name: "John Doe", admin: true, age: 30 })
    print("json " + j)
    d = json_decode(j)
    print("json_rt " + d.sub + " | " + d.name + " | " + string(d.admin) + " | " + string(d.age))
    print("json_bad " + string(json_decode("{not json")))

    ' --- JWT HS256 (matches PyJWT byte-for-byte) ---
    tok = jwt_encode({ sub: "1234567890", name: "John Doe", admin: true }, secret)
    print("jwt " + tok)
    pv = jwt_verify(tok, secret)
    print("jwt_rt " + pv.sub + " | " + pv.name + " | " + string(pv.admin))
    print("jwt_badsecret " + string(jwt_verify(tok, "nope")))
    print("jwt_malformed " + string(jwt_verify("a.b", secret)))

    ' exp enforcement (round epoch values survive %g formatting exactly):
    ' 4000000000 = year 2096 (valid), 1000000000 = year 2001 (expired)
    live = jwt_encode({ sub: "u1", exp: 4000000000 }, secret)
    print("jwt_exp_live " + jwt_verify(live, secret).sub)
    dead = jwt_encode({ sub: "u1", exp: 1000000000 }, secret)
    print("jwt_exp_expired " + string(jwt_verify(dead, secret)))

    ' --- symmetric encrypt/decrypt (random nonce packed in the blob) ---
    key = hex_decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
    msg = "the launch code is 0000"
    blob = encrypt(key, msg)
    ' 12 nonce + len(msg) ciphertext + 16 tag
    print("enc_len " + string(byte_count(blob)))
    print("dec " + decrypt(key, blob))
    wrongkey = hex_decode("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100")
    print("dec_wrongkey " + string(decrypt(wrongkey, blob)))
    print("dec_short " + string(decrypt(key, "tiny")))
end program
