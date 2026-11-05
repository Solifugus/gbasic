' THE PRIMITIVES SSH NEEDS (docs/crypto_design.md §9): X25519, RSA over the
' components SSH carries, and AES-CTR with the counter handed back. Asked for
' by Transward, which is writing its own SSH/SFTP in gBASIC.
'
' THREE KINDS OF ORACLE, and the first is the one that does not share OpenSSL:
'   - published vectors: RFC 7748 for X25519, NIST SP 800-38A for AES-CTR;
'   - Python's `cryptography` (tests/crypto_ssh_oracle.py `make`): keys,
'     PEM files and signatures gBASIC must read and agree with. PKCS#1 v1.5
'     is deterministic, so gBASIC's signature must equal Python's BYTE FOR
'     BYTE, not merely verify;
'   - the runner's `check` stage, where Python USES what gBASIC made.
'
' Reads $DIR/py.json, writes $DIR/gb.json for the check stage.

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

function refusal(verb, a, b, c)
    on error goto handle
    if verb = "x25519_shared" then
        r = x25519_shared(a, b)
    else if verb = "x25519_public" then
        r = x25519_public(a)
    else if verb = "rsa_sign" then
        r = rsa_sign(a, b, c)
    else if verb = "rsa_verify" then
        r = rsa_verify(a, b, c, "sha256")
    else if verb = "rsa_from_pem" then
        if b = nothing then
            r = rsa_from_pem(a)
        else
            r = rsa_from_pem(a, b)
        end if
    else if verb = "rsa_generate" then
        r = rsa_generate(a)
    else if verb = "aes_ctr" then
        r = aes_ctr(a, b, c)
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

' a key record with every field hex-decoded
function key_of(r)
    k = {}
    for each f in keys(r)
        k[f] = un(r[f])
    end for
    return k
end function

function key_hex(k)
    r = {}
    for each f in keys(k)
        r[f] = hx(k[f])
    end for
    return r
end function

g = { checks: 0, bad: 0 }
dir = env("DIR")
py = decode(read(join_path(dir, "py.json")))
gb = {}

print "-- X25519: RFC 7748"
alice = un("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a")
bob = un("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb")
check("§6.1 Alice's public key", hx(x25519_public(alice)),
      "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a")
check("§6.1 Bob's public key", hx(x25519_public(bob)),
      "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f")
check("§6.1 the shared secret, Alice's side",
      hx(x25519_shared(alice, x25519_public(bob))),
      "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")
check("  and Bob's", hx(x25519_shared(bob, x25519_public(alice))),
      "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")
check("§5.2 scalar times a u-coordinate",
      hx(x25519_shared(un("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4"),
                       un("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c"))),
      "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552")
check("§6.1: a peer sending zero gives an all-zero secret, which raises",
      refusal("x25519_shared", alice, repeat(chr(0), 32), nothing),
      "x25519_shared: the peer's public key gives an all-zero secret (a low-order point, refused by RFC 7748 section 6.1)")
check("  and so does a low-order point (u = 1)",
      refusal("x25519_shared", alice, chr(1) + repeat(chr(0), 31), nothing),
      "x25519_shared: the peer's public key gives an all-zero secret (a low-order point, refused by RFC 7748 section 6.1)")

print "-- X25519: against Python"
check("Python's private key gives Python's public key", hx(x25519_public(un(py.x_a_priv))), py.x_a_pub)
check("and the secret Python computed", hx(x25519_shared(un(py.x_a_priv), un(py.x_b_pub))), py.x_shared)
kp = x25519_keypair()
check("a generated pair: 32 and 32 bytes", [ byte_count(kp.public), byte_count(kp.private) ], [ 32, 32 ])
check("  whose public key is its private key's", kp.public = x25519_public(kp.private), true)
check("  and a second pair is a different one", x25519_keypair().private = kp.private, false)
gb.x_pub = hx(kp.public)
gb.x_shared_with_b = hx(x25519_shared(kp.private, un(py.x_b_pub)))
check("a short key is refused", refusal("x25519_shared", "short", kp.public, nothing),
      "x25519_shared: expects our 32-byte private key and the peer's 32-byte public key")
check("so is a short private key", refusal("x25519_public", "short", nothing, nothing),
      "x25519_public: the private key must be 32 bytes")

print "-- RSA: reading every form a key arrives in"
want = py.rsa
check("PKCS#1 PEM gives the six parts", key_hex(rsa_from_pem(py.pem_pkcs1)), want)
check("PKCS#8 PEM gives the same", key_hex(rsa_from_pem(py.pem_pkcs8)), want)
check("encrypted PKCS#8 with its passphrase", key_hex(rsa_from_pem(py.pem_pkcs8_enc, "correct horse")), want)
check("a public key as SubjectPublicKeyInfo", key_hex(rsa_from_pem(py.pem_pub_spki)), { n: want.n, e: want.e })
check("a public key as PKCS#1", key_hex(rsa_from_pem(py.pem_pub_pkcs1)), { n: want.n, e: want.e })
check("an encrypted key without its passphrase is refused",
      refusal("rsa_from_pem", py.pem_pkcs8_enc, nothing, nothing),
      "rsa_from_pem: no RSA key could be read from that text (an encrypted key needs its passphrase)")
check("and with the wrong one",
      refusal("rsa_from_pem", py.pem_pkcs8_enc, "wrong", nothing),
      "rsa_from_pem: no RSA key could be read from that text with that passphrase")
check("text that is not a key", refusal("rsa_from_pem", "hello", nothing, nothing),
      "rsa_from_pem: no RSA key could be read from that text (an encrypted key needs its passphrase)")

print "-- RSA: signing, byte for byte"
key = key_of(want)
msg = un(py.msg)
check("rsa-sha2-256 equals Python's signature exactly", hx(rsa_sign(key, msg, "sha256")), py.sig_sha256)
check("rsa-sha2-512 too", hx(rsa_sign(key, msg, "sha512")), py.sig_sha512)
check("a signature is the modulus's length", byte_count(rsa_sign(key, msg, "sha256")), byte_count(key.n))
pub = rsa_public(key)
check("rsa_public keeps n and e only", keys(pub), [ "n", "e" ])
check("Python's sha256 signature verifies", rsa_verify(pub, msg, un(py.sig_sha256), "sha256"), true)
check("Python's sha512 signature verifies", rsa_verify(pub, msg, un(py.sig_sha512), "sha512"), true)
check("ssh-rsa: Python's SHA-1 signature verifies", rsa_verify(pub, msg, un(py.sig_sha1), "sha1"), true)
check("a changed message does not", rsa_verify(pub, msg + "x", un(py.sig_sha256), "sha256"), false)
sig = un(py.sig_sha256)
bad = byte_slice(sig, 0, 10) + chr(bxor(byte_at(sig, 10), 1)) + byte_slice(sig, 11, byte_count(sig) - 11)
check("a signature with one bit flipped does not", rsa_verify(pub, msg, bad, "sha256"), false)
check("a signature checked under the wrong hash does not", rsa_verify(pub, msg, un(py.sig_sha256), "sha512"), false)
check("the private key verifies as well as the public one", rsa_verify(key, msg, sig, "sha256"), true)
check("SHA-1 does not sign", refusal("rsa_sign", key, msg, "sha1"),
      "rsa_sign: will not sign with SHA-1 (ssh-rsa is verify-only); use \"sha256\" or \"sha512\"")
check("an unknown hash", refusal("rsa_sign", key, msg, "md5"),
      "rsa_sign: expects the hash as \"sha256\" or \"sha512\"")
check("a public key does not sign", refusal("rsa_sign", pub, msg, "sha256"),
      "rsa_sign: this is a public key (it has no `d`); signing needs the private key")
swapped = key_of(want)
swapped.p = key.q
swapped.q = key.p
check("parts that do not belong together are refused, not signed with",
      refusal("rsa_sign", swapped, msg, "sha256"),
      "rsa_sign: the key's parts do not belong together (n, e, d, p, q and iqmp must be one RSA key)")
missing = { n: key.n, e: key.e, d: key.d }
check("a private key missing a part names it", refusal("rsa_sign", missing, msg, "sha256"),
      "rsa_sign: the key has no `p`")
check("a key that is not a record", refusal("rsa_verify", "key", msg, sig),
      "rsa_verify: the key must be a record { n, e }")

print "-- RSA: a generated key"
gen = rsa_generate(2048)
check("2048 bits is a 256-byte modulus", byte_count(gen.n), 256)
check("e is 65537", hx(gen.e), "010001")
check("it has the six parts", keys(gen), [ "n", "e", "d", "p", "q", "iqmp" ])
gsig = rsa_sign(gen, msg, "sha256")
check("it signs, and its public half verifies", rsa_verify(rsa_public(gen), msg, gsig, "sha256"), true)
check("its PEM reads back as the same key", key_hex(rsa_from_pem(rsa_to_pem(gen))), key_hex(gen))
check("  as does its public PEM", key_hex(rsa_from_pem(rsa_to_pem(rsa_public(gen)))), key_hex(rsa_public(gen)))
check("1024 bits is refused", refusal("rsa_generate", 1024, nothing, nothing),
      "rsa_generate: expects the size in bits, a multiple of 8 from 2048 to 16384 (2048, 3072 or 4096)")
gb.gen = key_hex(gen)
gb.gen_bits = 2048
gb.gen_pem = rsa_to_pem(gen)
gb.gen_pub_pem = rsa_to_pem(rsa_public(gen))
gb.gen_sig = hx(gsig)
gb.sig_sha256 = hx(rsa_sign(key, msg, "sha256"))
gb.sig_sha512 = hx(rsa_sign(key, msg, "sha512"))

print "-- AES-CTR: NIST SP 800-38A"
plain = un("6bc1bee22e409f96e93d7e117393172a" + "ae2d8a571e03ac9c9eb76fac45af8e51" +
           "30c81c46a35ce411e5fbc1191a0a52ef" + "f69f2445df4f9b17ad2b417be66c3710")
ctr0 = un("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff")
r = aes_ctr(un("2b7e151628aed2a6abf7158809cf4f3c"), ctr0, plain)
check("F.5.1 CTR-AES128", hx(r.data),
      "874d6191b620e3261bef6864990db6ce" + "9806f66b7970fdff8617187bb9fffdff" +
      "5ae4df3edbd5d35e5b4f09020db03eab" + "1e031dda2fbe03d1792170a0f3009cee")
check("  and the counter four blocks on, carried across a byte", hx(r.counter),
      "f0f1f2f3f4f5f6f7f8f9fafbfcfdff03")
r = aes_ctr(un("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"), ctr0, plain)
check("F.5.5 CTR-AES256", hx(r.data),
      "601ec313775789a5b7a7f504bbf3d228" + "f443e3ca4d62b59aca84e990cacaf5c5" +
      "2b0930daa23de94ce87017ba2d84988d" + "dfc9c58db67aada613c2dd08457941a6")
check("a partial block uses the whole block: 7 bytes move the counter by one",
      hx(aes_ctr(un("2b7e151628aed2a6abf7158809cf4f3c"), ctr0, "7 bytes").counter),
      "f0f1f2f3f4f5f6f7f8f9fafbfcfdff00")
check("decrypting is the same operation", aes_ctr(un("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"), ctr0, r.data).data, plain)
check("the counter wraps at 2^128", hx(aes_ctr(un("2b7e151628aed2a6abf7158809cf4f3c"),
      un(repeat("ff", 16)), "sixteen bytes!!!").counter), repeat("00", 16))

print "-- AES-CTR: one keystream across packets"
k = un(py.ctr_key)
data = un(py.ctr_data)
c = un(py.ctr_iv)
p1 = aes_ctr(k, c, byte_slice(data, 0, 16))
p2 = aes_ctr(k, p1.counter, byte_slice(data, 16, 32))
p3 = aes_ctr(k, p2.counter, byte_slice(data, 48, byte_count(data) - 48))
chained = p1.data + p2.data + p3.data
check("three packets chained by the returned counter are Python's one stream", hx(chained), py.ctr_ct)
check("the low 64 bits carried into the high ones", hx(p2.counter), "00112233445566780000000000000001")
gb.ctr_plain = py.ctr_data
gb.ctr_chained = hx(chained)
check("a 15-byte key is refused", refusal("aes_ctr", "fifteen bytes!!", c, "x"),
      "aes_ctr: expects a 16, 24 or 32-byte key, a 16-byte counter block and the data")
check("so is a short counter", refusal("aes_ctr", k, "short", "x"),
      "aes_ctr: expects a 16, 24 or 32-byte key, a 16-byte counter block and the data")

write(join_path(dir, "gb.json"), json_encode(gb))
print ""
print "checks: " + string(g.checks)
print "mismatches: " + string(g.bad)
