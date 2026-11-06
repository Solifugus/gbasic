' THE REST OF WHAT SSH NEEDS (docs/crypto_design.md §9, Transward's "Needed"
' list, 2.5-2.9): ChaCha20 and Poly1305 as raw parts, ECDSA and ECDH on the
' NIST curves, classic Diffie-Hellman, HKDF, incremental hashing, and sha384.
'
' THE ORACLES SHARE AS LITTLE WITH OpenSSL AS COULD BE ARRANGED:
'   - published vectors here: RFC 8439, RFC 5869, FIPS 180-2, RFC 4231;
'   - tests/crypto_ssh_oracle.py `make`, where ChaCha20, Poly1305 and HKDF
'     are written in Python from the RFCs (and held to their vectors before
'     use), Diffie-Hellman is Python's `pow` over groups computed from RFC
'     3526's formula, hashing is hashlib, and the curves are Python's
'     `cryptography`;
'   - its `check` stage, where Python USES what gBASIC made: verifies its
'     ECDSA signatures, agrees secrets with its keys, and opens a
'     chacha20-poly1305@openssh.com packet gBASIC built from the parts.
'
' Reads $DIR/py.json, writes $DIR/gb2.json for the check stage.

function check(what, got, want)
    g.checks = g.checks + 1
    if string(got) = string(want) then
        print "ok   " + what
    else
        g.bad = g.bad + 1
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' the message a call raises, or ACCEPTED
function refusal(verb, a, b, c)
    on error goto handle
    if verb = "chacha20" then
        r = chacha20(a, b, c)
    else if verb = "poly1305" then
        r = poly1305(a, b)
    else if verb = "ec_keypair" then
        r = ec_keypair(a)
    else if verb = "ec_public" then
        r = ec_public(a, b)
    else if verb = "ecdsa_sign" then
        r = ecdsa_sign(a, b)
    else if verb = "ecdsa_verify" then
        r = ecdsa_verify(a, b, c)
    else if verb = "ecdh_shared" then
        r = ecdh_shared(a, b)
    else if verb = "dh_keypair" then
        r = dh_keypair(a, b)
    else if verb = "dh_shared" then
        r = dh_shared(a, b, c)
    else if verb = "hkdf" then
        r = hkdf(a, "ikm", "", "", b)
    else if verb = "hkdf_ikm" then
        r = hkdf("sha256", a, "", "", 32)
    else if verb = "hash_start" then
        r = hash_start(a)
    else if verb = "hash_update" then
        r = hash_update(a, b)
    else if verb = "hash_finish" then
        r = hash_finish(a)
    end if
    return "ACCEPTED"
handle:
    m = error.message
    error.clear()
    return m
end function

function hx(s)
    return hex_encode(s)
end function

function un(s)
    return hex_decode(s)
end function

function zeros(n)
    return un(repeat("00", n))
end function

' `s` with its last byte changed
function flip_last(s)
    n = byte_count(s)
    last = byte_at(s, n - 1)
    if last = 0 then
        return byte_slice(s, 0, n - 1) + un("01")
    end if
    return byte_slice(s, 0, n - 1) + un("00")
end function

g = { checks: 0, bad: 0 }
dir = env("DIR")
py = decode(read(join_path(dir, "py.json")))
gb = {}

print "-- Poly1305: RFC 8439 2.5.2, and the RFC-written Python"
check("RFC 8439 2.5.2's tag",
      hx(poly1305(un("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b"),
                  "Cryptographic Forum Research Group")),
      "a8061dc1305136c6c22b8baf0c0127a9")
for each c in py.poly
    check("a " + string(byte_count(un(c.msg))) + "-byte message",
          hx(poly1305(un(c.key), un(c.msg))), c.tag)
end for
check("a short key is refused", refusal("poly1305", zeros(31), "m", nothing),
      "poly1305: expects a 32-byte one-time key and the message")

print "-- ChaCha20: RFC 8439 2.6.2, and a counter that wraps its low word"
' 2.6.2 derives the Poly1305 key: the first 32 bytes of block 0
k = un("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f")
check("RFC 8439 2.6.2's one-time key",
      hx(byte_slice(chacha20(k, un("00000000000000000001020304050607"), zeros(32)), 0, 32)),
      "8ad5a08b905f81cc815040274ab29471a833b637e3fd0da508dbb8e2fdd1a646")
' THE COUNTER IS 64 BITS: the low word starts two blocks from wrapping, and
' the stream must be the one whose counter carries into the next word --
' OpenSSH's layout (64-bit counter, 64-bit nonce), not RFC 8439's 32-bit one.
cc = chacha20(un(py.cc_key), un(py.cc_iv), un(py.cc_data))
check("the stream across the counter's low word is OpenSSH's (it carries)", hx(cc), py.cc_djb)
check("and is not RFC 8439's 32-bit counter (the two differ here)", hx(cc) = py.cc_ietf, false)
check("deciphering is the same operation",
      hx(chacha20(un(py.cc_key), un(py.cc_iv), cc)), py.cc_data)
check("a 12-byte IV is refused (the 16 bytes are the counter and the nonce)",
      refusal("chacha20", zeros(32), zeros(12), "d"),
      "chacha20: expects a 32-byte key, a 16-byte IV (the block counter, 4 bytes little-endian, then the 12-byte nonce) and the data")
check("a 16-byte key is refused", refusal("chacha20", zeros(16), zeros(16), "d"),
      "chacha20: expects a 32-byte key, a 16-byte IV (the block counter, 4 bytes little-endian, then the 12-byte nonce) and the data")

print "-- chacha20-poly1305@openssh.com, built from the parts (PROTOCOL.chacha20poly1305)"
key = random_bytes(64)
k_main = byte_slice(key, 0, 32)
k_header = byte_slice(key, 32, 32)
seq = un("0000000000000007")
payload = random_bytes(37)
enc_len = chacha20(k_header, zeros(8) + seq, un("00000025"))
enc_body = chacha20(k_main, un("0100000000000000") + seq, payload)
polykey = chacha20(k_main, zeros(8) + seq, zeros(32))
sealed = enc_len + enc_body + poly1305(polykey, enc_len + enc_body)
check("the packet is length, payload and a 16-byte tag", byte_count(sealed), 4 + 37 + 16)
gb.openssh_packet = { key: hx(key), seqnr: 7, plain: hx(payload), sealed: hx(sealed) }

print "-- ECDSA and ECDH on the NIST curves, against Python's keys"
sizes = { p256: 32, p384: 48, p521: 66 }
for each name in [ "p256", "p384", "p521" ]
    c = py.ec[name]
    size = sizes[name]
    mine = { curve: name, public: un(c.public), private: un(c.private) }
    pub = { curve: name, public: un(c.public) }
    check(name + ": ec_public of Python's scalar is Python's point",
          hx(ec_public(name, un(c.private)).public), c.public)
    sig = { r: un(c.r), s: un(c.s) }
    check(name + ": Python's signature verifies", ecdsa_verify(pub, un(c.msg), sig), true)
    check(name + ": not over another message", ecdsa_verify(pub, un(c.msg) + "x", sig), false)
    check(name + ": not with s changed",
          ecdsa_verify(pub, un(c.msg), { r: sig.r, s: flip_last(sig.s) }), false)
    check(name + ": the secret with Python's peer is Python's",
          hx(ecdh_shared(mine, un(c.peer))), c.shared)
    check(name + ": a point off the curve is refused (the invalid-curve attack)",
          refusal("ecdh_shared", mine, flip_last(un(c.peer)), nothing),
          "ecdh_shared: that point is not on " + name)
    check(name + ": a key whose private part is another key's is refused",
          refusal("ecdsa_sign", { curve: name, public: un(c.peer), private: un(c.private) }, "m", nothing),
          "ecdsa_sign: the private and public parts are not one " + name + " key")
    k = ec_keypair(name)
    check(name + ": a generated key's scalar is padded to the curve's size", byte_count(k.private), size)
    check(name + ": its point is uncompressed", byte_count(k.public), 1 + 2 * size)
    check(name + ": and is its scalar's", ec_public(name, k.private).public = k.public, true)
    msg = random_bytes(40)
    s = ecdsa_sign(k, msg)
    check(name + ": its signature verifies here", ecdsa_verify({ curve: name, public: k.public }, msg, s), true)
    gb["ec_" + name] = { public: hx(k.public), msg: hx(msg), r: hx(s.r), s: hx(s.s),
                         shared: hx(ecdh_shared(k, un(c.peer))) }
end for
check("an unknown curve is refused", refusal("ec_keypair", "p999", nothing, nothing),
      "ec_keypair: expects the curve as \"p256\", \"p384\" or \"p521\"")
check("a compressed point is refused",
      refusal("ecdsa_verify", { curve: "p256", public: un("02" + repeat("11", 32)) }, "m", { r: "1", s: "1" }),
      "ecdsa_verify: the public key must be an uncompressed p256 point (65 bytes beginning 04)")
check("a public key cannot sign", refusal("ecdsa_sign", { curve: "p256", public: k.public }, "m", nothing),
      "ecdsa_sign: this is a public key (it has no `private`); it cannot sign or agree")
check("a signature that is not { r, s }",
      refusal("ecdsa_verify", { curve: "p256", public: un(py.ec.p256.public) }, "m", "sig"),
      "ecdsa_verify: expects the signature as a record { r, s } of big-endian bytes")
check("a zero scalar has no public key", refusal("ec_public", "p256", zeros(32), nothing),
      "ec_public: that scalar is not a private key on the curve")

print "-- classic Diffie-Hellman over RFC 3526's groups, against Python's pow"
for each group in [ "14", "16", "18" ]
    d = py.dh[group]
    check("group " + group + ": the secret is Python's",
          hx(dh_shared(un(d.p), un(d.x), un(d.y))), d.shared)
end for
for each group in [ "14", "16" ]
    d = py.dh[group]
    k = dh_keypair(un(d.p), un("02"))
    gb["dh_" + group] = { private: hx(k.private), public: hx(k.public),
                          shared: hx(dh_shared(un(d.p), k.private, un(d.y))) }
end for
p14 = un(py.dh["14"].p)
pm1 = byte_slice(p14, 0, byte_count(p14) - 1) + un("fe")
x14 = un(py.dh["14"].x)
peer_refused = "dh_shared: the peer's public value must be greater than 1 and less than p - 1"
check("a peer value of 1 is refused (RFC 4253 §8)", refusal("dh_shared", p14, x14, un("01")), peer_refused)
check("a peer value of 0", refusal("dh_shared", p14, x14, un("00")), peer_refused)
check("a peer value of p - 1", refusal("dh_shared", p14, x14, pm1), peer_refused)
check("a peer value of p", refusal("dh_shared", p14, x14, p14), peer_refused)
check("2, the smallest allowed, is accepted", refusal("dh_shared", p14, x14, un("02")), "ACCEPTED")
check("a 1024-bit prime is refused", refusal("dh_keypair", byte_slice(p14, 0, 128), un("02"), nothing),
      "dh_keypair: expects the group's prime p (at least 2048 bits) and its generator g, as big-endian bytes")

print "-- HKDF: RFC 5869 A.1, and HMAC-composed Python for every hash"
check("RFC 5869 A.1's OKM",
      hx(hkdf("sha256", un(repeat("0b", 22)), un("000102030405060708090a0b0c"),
              un("f0f1f2f3f4f5f6f7f8f9"), 42)),
      "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865")
for each c in py.hkdf
    check(c.hash + ", " + string(c.length) + " bytes, salt " + string(byte_count(un(c.salt))) +
          ", info " + string(byte_count(un(c.info))),
          hx(hkdf(c.hash, un(c.ikm), un(c.salt), un(c.info), c.length)), c.okm)
end for
check("one byte past 255 times sha256's size is refused", refusal("hkdf", "sha256", 8161, nothing),
      "hkdf: expects the length in bytes, a whole number from 1 to 8160 (255 times sha256's size)")
check("and past sha384's", refusal("hkdf", "sha384", 12241, nothing),
      "hkdf: expects the length in bytes, a whole number from 1 to 12240 (255 times sha384's size)")
check("an unknown hash", refusal("hkdf", "md5", 32, nothing),
      "hkdf: expects the hash as \"sha256\", \"sha384\" or \"sha512\"")
check("empty key material", refusal("hkdf_ikm", "", nothing, nothing),
      "hkdf: expects the key material (not empty), the salt and the info as bytes")

print "-- sha384 and hmac_sha384: FIPS 180-2, RFC 4231, hashlib"
check("FIPS 180-2's sha384(\"abc\")", hx(sha384("abc")),
      "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7")
check("RFC 4231 test case 1", hx(hmac_sha384(un(repeat("0b", 20)), "Hi There")),
      "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59cfaea9ea9076ede7f4af152e8b2fa9cb6")
check("a key longer than the block, against Python's hmac",
      hx(hmac_sha384(un(py.hmac384.key), un(py.hmac384.msg))), py.hmac384.mac)

print "-- incremental hashing: chunks across every block boundary, against hashlib"
data = un(py.hash_data)
for each alg in [ "sha256", "sha384", "sha512" ]
    s = hash_start(alg)
    at = 0
    for each n in [ 1, 63, 64, 65, 127, 128, 129, 300, 123 ]
        s = hash_update(s, byte_slice(data, at, n))
        at = at + n
    end for
    check(alg + ": 1000 bytes in nine pieces", hx(hash_finish(s)), py.hash[alg])
end for
s = hash_update(hash_start("sha256"), "ab")
check("A STATE IS A VALUE: two continuations of one state are independent",
      hx(hash_finish(hash_update(s, "c"))) + " " + hx(hash_finish(hash_update(s, "d"))),
      hx(sha256("abc")) + " " + hx(sha256("abd")))
check("and finishing it does not consume it", hx(hash_finish(s)), hx(sha256("ab")))
check("an empty hash", hx(hash_finish(hash_start("sha512"))), hx(sha512("")))
check("an empty update changes nothing", hx(hash_finish(hash_update(s, ""))), hx(sha256("ab")))
n = byte_count(s)
not_state = "hash_update: expects a hash state from hash_start or hash_update"
' OpenSSL's context ends `unsigned int num, md_len`: num, the position in the
' block buffer, alone is forged, since md_len is checked too and would refuse
' a state with both changed even if num went unchecked.
check("A FORGED STATE IS REFUSED: its block position past the block",
      refusal("hash_update", byte_slice(s, 0, n - 8) + un("ffffffff") + byte_slice(s, n - 4, 4), "x", nothing),
      not_state)
check("and its digest length",
      refusal("hash_update", byte_slice(s, 0, n - 4) + un("ff000000"), "x", nothing), not_state)
check("a state cut short", refusal("hash_update", byte_slice(s, 0, n - 1), "x", nothing), not_state)
check("a state relabelled as another hash",
      refusal("hash_update", byte_slice(s, 0, 7) + un("03") + byte_slice(s, 8, n - 8), "x", nothing),
      not_state)
check("text that is not a state", refusal("hash_finish", "hello", nothing, nothing),
      "hash_finish: expects a hash state from hash_start or hash_update")
check("an unknown hash", refusal("hash_start", "md5", nothing, nothing),
      "hash_start: expects the hash as \"sha256\", \"sha384\" or \"sha512\"")

write(join_path(dir, "gb2.json"), json_encode(gb))
print ""
print "checks: " + string(g.checks)
print "mismatches: " + string(g.bad)
