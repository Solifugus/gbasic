' crypto foundation — encoding, hashing, HMAC, constant-time compare
' (docs/crypto_design.md Phases 1-2). Digests/HMAC verified against Python
' hashlib/hmac and the openssl CLI; base64/hex against known vectors. Binary
' outputs are hex-encoded before printing so the golden output is stable and
' cross-architecture deterministic.
program demo(args)
    m = "hello world"

    ' --- base64 / hex round-trips and known vectors ---
    print("b64 " + base64_encode(m))
    print("b64rt " + base64_decode(base64_encode(m)))
    print("hex " + hex_encode(m))
    print("hexrt " + hex_decode(hex_encode(m)))

    ' base64url: URL-safe alphabet, no padding; high bytes exercise '-' '_'
    raw = from_bytes([255, 254, 253, 0, 16])
    print("b64url " + base64url_encode(raw))
    print("b64url_rt " + hex_encode(base64url_decode(base64url_encode(raw))))
    ' base64_decode also accepts the url alphabet and missing padding
    print("b64_cross " + hex_encode(base64_decode(base64url_encode(raw))))

    ' empty input is valid
    print("b64_empty [" + base64_encode("") + "]")
    print("hex_empty [" + hex_encode("") + "]")

    ' --- digests (hex) ---
    print("sha256 " + hex_encode(sha256(m)))
    print("sha512 " + hex_encode(sha512(m)))
    print("sha1 " + hex_encode(sha1(m)))
    print("md5 " + hex_encode(md5(m)))
    print("sha256_empty " + hex_encode(sha256("")))

    ' --- HMAC (hex) ---
    print("hmac256 " + hex_encode(hmac_sha256("key", m)))
    print("hmac512 " + hex_encode(hmac_sha512("key", m)))

    ' --- constant-time compare ---
    print("eq_same " + string(bytes_equal("secret", "secret")))
    print("eq_diff " + string(bytes_equal("secret", "secrduh")))
    print("eq_len " + string(bytes_equal("secret", "secret!")))
    print("eq_dig " + string(bytes_equal(sha256(m), sha256("hello world"))))

    ' --- random_bytes: length only (non-deterministic content) ---
    print("rb16 " + string(byte_count(random_bytes(16))))
    print("rb1 " + string(byte_count(random_bytes(1))))

    ' --- malformed input returns unknown ---
    print("bad_b64 " + string(base64_decode("@@@@")))
    print("bad_hex_odd " + string(hex_decode("abc")))
    print("bad_hex_char " + string(hex_decode("zz")))
end program
