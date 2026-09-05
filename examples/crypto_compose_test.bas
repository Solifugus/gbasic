' crypto.bas Phase 3 — ergonomic compositions (docs/crypto_design.md §5).
' Hex digests, signed cookies, CSRF tokens, flat-JSON, JWT/HS256, and
' nonce-managing symmetric encrypt/decrypt. The JWT token is byte-for-byte
' identical to PyJWT's HS256 output for the same payload and secret.
program demo(args)
    load crypto from "../stdlib/crypto.bas"
    secret = "topsecret"

    print("sha256_hex " + crypto.sha256_hex("hello world"))

    ' --- signed cookies ---
    c = crypto.sign_cookie("user=42", secret)
    print("cookie " + c)
    print("cookie_rt " + crypto.verify_cookie(c, secret))
    print("cookie_badsecret " + string(crypto.verify_cookie(c, "wrong")))
    print("cookie_tamper " + string(crypto.verify_cookie(c + "x", secret)))
    print("cookie_malformed " + string(crypto.verify_cookie("nodot", secret)))

    ' --- CSRF ---
    t = crypto.csrf_token(secret, "sess-abc")
    print("csrf " + t)
    print("csrf_ok " + string(crypto.csrf_check(t, secret, "sess-abc")))
    print("csrf_badsession " + string(crypto.csrf_check(t, secret, "sess-xyz")))

    ' --- JSON: the core builtin encodes, crypto's strict flat parser reads ---
    ' `json_encode` here is the BUILTIN. crypto shipped its own flat encoder
    ' until 0.1.0-rc8, and from the moment json_encode became a builtin this
    ' very line reached the builtin instead -- the library's copy was
    ' unreachable except when spelled `crypto.json_encode`, and the runtime
    ' warned on every load that it was being shadowed. It is gone.
    j = json_encode({ sub: "1234567890", name: "John Doe", admin: true, age: 30 })
    print("json " + j)
    d = crypto.json_decode(j)
    print("json_rt " + d.sub + " | " + d.name + " | " + string(d.admin) + " | " + string(d.age))
    print("json_bad " + string(crypto.json_decode("{not json")))
    ' The decoder is STRICT on purpose: it reads attacker-supplied token
    ' payloads, so the gBASIC dialect tokens `try_decode` accepts are refused.
    print("json_dialect " + string(crypto.json_decode("{\"a\":nothing}")))
    print("json_inf " + string(crypto.json_decode("{\"a\":inf}")))

    ' --- JWT HS256 (matches PyJWT byte-for-byte) ---
    tok = crypto.jwt_encode({ sub: "1234567890", name: "John Doe", admin: true }, secret)
    print("jwt " + tok)
    pv = crypto.jwt_verify(tok, secret)
    print("jwt_rt " + pv.sub + " | " + pv.name + " | " + string(pv.admin))
    print("jwt_badsecret " + string(crypto.jwt_verify(tok, "nope")))
    print("jwt_malformed " + string(crypto.jwt_verify("a.b", secret)))
    ' The encoder is the core builtin and handles any depth; the DECODER here
    ' is flat by design. So jwt_encode refuses a nested claim rather than
    ' minting a token jwt_verify would answer `unknown` for -- a signer that can
    ' produce what its own verifier cannot read is the round-trip hole rc7
    ' closed one layer down, and it must not reappear here.
    print("jwt_nested " + string(crypto.jwt_encode({ sub: "u1", scope: { read: true } }, secret)))
    ' And a claim JSON cannot represent is REFUSED rather than quietly signed:
    ' the old flat encoder wrote `unknown` out as null, conflating "not known"
    ' with "no value" inside a signed token.
    print("jwt_unrepresentable " + string(crypto.jwt_encode({ sub: "u1", na: unknown }, secret)))

    ' exp enforcement (round epoch values survive %g formatting exactly):
    ' 4000000000 = year 2096 (valid), 1000000000 = year 2001 (expired)
    live = crypto.jwt_encode({ sub: "u1", exp: 4000000000 }, secret)
    print("jwt_exp_live " + crypto.jwt_verify(live, secret).sub)
    dead = crypto.jwt_encode({ sub: "u1", exp: 1000000000 }, secret)
    print("jwt_exp_expired " + string(crypto.jwt_verify(dead, secret)))

    ' --- symmetric encrypt/decrypt (random nonce packed in the blob) ---
    key = hex_decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
    msg = "the launch code is 0000"
    blob = crypto.encrypt(key, msg)
    ' 12 nonce + len(msg) ciphertext + 16 tag
    print("enc_len " + string(byte_count(blob)))
    print("dec " + crypto.decrypt(key, blob))
    wrongkey = hex_decode("ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100")
    print("dec_wrongkey " + string(crypto.decrypt(wrongkey, blob)))
    print("dec_short " + string(crypto.decrypt(key, "tiny")))
end program
