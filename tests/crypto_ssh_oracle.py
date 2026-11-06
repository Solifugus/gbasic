#!/usr/bin/env python3
"""The other side of tests/run_crypto_ssh.sh, written with Python's
`cryptography` package.

    crypto_ssh_oracle.py make DIR     write DIR/py.json: keys, messages and
                                      Python's own answers, hex-encoded
    crypto_ssh_oracle.py check DIR    read DIR/gb.json, gBASIC's answers, and
                                      check each one the way a peer would

`make` gives gBASIC something to agree with; `check` takes what gBASIC made
and uses it -- a signature verified, a public key used in an exchange, a PEM
loaded -- so a value that only LOOKS right fails here. Prints `ok` or `FAIL`
lines; the runner counts them.
"""
import hashlib
import hmac
import json
import os
import random
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, padding, rsa, x25519
from cryptography.hazmat.primitives.asymmetric.utils import (decode_dss_signature,
                                                             encode_dss_signature)
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

H = bytes.hex
U = bytes.fromhex


# ---- WRITTEN HERE FROM THE RFCs, NOT CALLED FROM A LIBRARY, so the second
# half of the suite (ChaCha20, Poly1305, HKDF) is checked against code that
# shares nothing with OpenSSL. Each is held to a published vector in
# selfcheck() before it is trusted as an oracle.
M = 0xffffffff
def _rotl(v, c): return ((v << c) & M) | (v >> (32 - c))
def _qr(s, a, b, c, d):
    s[a] = (s[a] + s[b]) & M; s[d] = _rotl(s[d] ^ s[a], 16)
    s[c] = (s[c] + s[d]) & M; s[b] = _rotl(s[b] ^ s[c], 12)
    s[a] = (s[a] + s[b]) & M; s[d] = _rotl(s[d] ^ s[a], 8)
    s[c] = (s[c] + s[d]) & M; s[b] = _rotl(s[b] ^ s[c], 7)
def chacha_block(key, words12_15):
    """RFC 8439 2.3: state words 12..15 are given directly, so one function
    serves the IETF layout (32-bit counter, 96-bit nonce) and the original
    one OpenSSH uses (64-bit counter, 64-bit nonce)."""
    s = [0x61707865, 0x3320646e, 0x79622d32, 0x6b206574] + list(struct.unpack("<8L", key)) + list(words12_15)
    w = s[:]
    for _ in range(10):
        _qr(w, 0, 4, 8, 12); _qr(w, 1, 5, 9, 13); _qr(w, 2, 6, 10, 14); _qr(w, 3, 7, 11, 15)
        _qr(w, 0, 5, 10, 15); _qr(w, 1, 6, 11, 12); _qr(w, 2, 7, 8, 13); _qr(w, 3, 4, 9, 14)
    return struct.pack("<16L", *[(x + y) & M for x, y in zip(w, s)])
def chacha20_djb(key, counter64, nonce8, data):
    out = bytearray()
    for i in range(0, len(data), 64):
        c = counter64 + i // 64
        ks = chacha_block(key, (c & M, c >> 32) + struct.unpack("<2L", nonce8))
        out += bytes(x ^ y for x, y in zip(data[i:i + 64], ks))
    return bytes(out)
def chacha20_ietf(key, counter32, nonce12, data):
    out = bytearray()
    for i in range(0, len(data), 64):
        ks = chacha_block(key, ((counter32 + i // 64) & M,) + struct.unpack("<3L", nonce12))
        out += bytes(x ^ y for x, y in zip(data[i:i + 64], ks))
    return bytes(out)
def poly1305(key, msg):
    r = int.from_bytes(key[:16], "little") & 0x0ffffffc0ffffffc0ffffffc0fffffff
    s = int.from_bytes(key[16:], "little")
    p = (1 << 130) - 5
    a = 0
    for i in range(0, len(msg), 16):
        a = ((a + int.from_bytes(msg[i:i + 16] + b"\x01", "little")) * r) % p
    return ((a + s) & ((1 << 128) - 1)).to_bytes(16, "little")
def hkdf(name, ikm, salt, info, n):
    h = getattr(hashlib, name)
    prk = hmac.new(salt or bytes(h().digest_size), ikm, h).digest()
    t, okm, i = b"", b"", 1
    while len(okm) < n:
        t = hmac.new(prk, t + info + bytes([i]), h).digest()
        okm += t; i += 1
    return okm[:n]


def selfcheck():
    k = bytes(range(0x80, 0xa0))
    assert chacha_block(k, (0,) + struct.unpack("<3L", U("000000000001020304050607")))[:32].hex() \
        == "8ad5a08b905f81cc815040274ab29471a833b637e3fd0da508dbb8e2fdd1a646", "RFC 8439 2.6.2"
    assert poly1305(U("85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b"),
                    b"Cryptographic Forum Research Group").hex() \
        == "a8061dc1305136c6c22b8baf0c0127a9", "RFC 8439 2.5.2"
    assert hkdf("sha256", U("0b" * 22), U("000102030405060708090a0b0c"),
                U("f0f1f2f3f4f5f6f7f8f9"), 42).hex() == (
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865"), "RFC 5869 A.1"


# RFC 3526's MODP groups (SSH's group14/16/18), COMPUTED from the RFC's
# formula p = 2^n - 2^(n-64) - 1 + 2^64 * (floor(2^(n-130) pi) + k) rather
# than pasted, and each checked to be a safe prime: a wrong k or a wrong pi
# would not give one.
def pi_fixed(bits):
    g = bits + 64
    one = 1 << g

    def atan_inv(x):
        s = t = one // x
        k, x2, sign = 1, x * x, -1
        while t:
            t //= x2
            k += 2
            s += sign * (t // k)
            sign = -sign
        return s
    return (16 * atan_inv(5) - 4 * atan_inv(239)) >> 64


def probable_prime(n, rounds):
    d, r = n - 1, 0
    while d % 2 == 0:
        d //= 2
        r += 1
    for _ in range(rounds):
        x = pow(random.randrange(2, n - 2), d, n)
        if x in (1, n - 1):
            continue
        for _ in range(r - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


MODP = {14: (2048, 124476), 16: (4096, 240904), 18: (8192, 4743158)}


def modp(group):
    n, k = MODP[group]
    p = 2**n - 2**(n - 64) - 1 + 2**64 * (pi_fixed(n - 130) + k)
    assert probable_prime(p, 1) and probable_prime((p - 1) // 2, 1), f"group {group}"
    return p


CURVES = {"p256": (ec.SECP256R1(), hashes.SHA256(), 32),
          "p384": (ec.SECP384R1(), hashes.SHA384(), 48),
          "p521": (ec.SECP521R1(), hashes.SHA512(), 66)}


def point(k):
    return k.public_key().public_bytes(serialization.Encoding.X962,
                                       serialization.PublicFormat.UncompressedPoint)


def be(n):
    return n.to_bytes((n.bit_length() + 7) // 8, "big").hex()


def make(d):
    out = {}
    # ---- X25519: two Python key pairs and their secret
    a = x25519.X25519PrivateKey.generate()
    b = x25519.X25519PrivateKey.generate()
    raw = serialization.Encoding.Raw
    out["x_a_priv"] = H(a.private_bytes(raw, serialization.PrivateFormat.Raw,
                                        serialization.NoEncryption()))
    out["x_a_pub"] = H(a.public_key().public_bytes(raw, serialization.PublicFormat.Raw))
    out["x_b_pub"] = H(b.public_key().public_bytes(raw, serialization.PublicFormat.Raw))
    out["x_shared"] = H(a.exchange(b.public_key()))
    out["x_b_priv"] = H(b.private_bytes(raw, serialization.PrivateFormat.Raw,
                                        serialization.NoEncryption()))

    # ---- RSA: one key in every form gBASIC must read
    k = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    n = k.private_numbers()
    out["rsa"] = {"n": be(n.public_numbers.n), "e": be(n.public_numbers.e),
                  "d": be(n.d), "p": be(n.p), "q": be(n.q), "iqmp": be(n.iqmp)}
    pem = serialization.Encoding.PEM
    out["pem_pkcs1"] = k.private_bytes(pem, serialization.PrivateFormat.TraditionalOpenSSL,
                                       serialization.NoEncryption()).decode()
    out["pem_pkcs8"] = k.private_bytes(pem, serialization.PrivateFormat.PKCS8,
                                       serialization.NoEncryption()).decode()
    out["pem_pkcs8_enc"] = k.private_bytes(
        pem, serialization.PrivateFormat.PKCS8,
        serialization.BestAvailableEncryption(b"correct horse")).decode()
    out["pem_pub_spki"] = k.public_key().public_bytes(
        pem, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    out["pem_pub_pkcs1"] = k.public_key().public_bytes(
        pem, serialization.PublicFormat.PKCS1).decode()
    msg = b"exchange hash \x00\x01\xff" + os.urandom(40)
    out["msg"] = H(msg)
    for name, h in (("sha256", hashes.SHA256()), ("sha512", hashes.SHA512()),
                    ("sha1", hashes.SHA1())):
        out["sig_" + name] = H(k.sign(msg, padding.PKCS1v15(), h))

    # ---- AES-CTR: a counter whose low 64 bits are about to carry, so the
    # stream crosses a byte boundary AND a 64-bit one (a counter kept in a
    # machine word would get the second wrong)
    key = os.urandom(32)
    ctr = U("0011223344556677" + "fffffffffffffffe")
    data = os.urandom(16 * 5 + 7)
    enc = Cipher(algorithms.AES(key), modes.CTR(ctr)).encryptor()
    out["ctr_key"] = H(key)
    out["ctr_iv"] = H(ctr)
    out["ctr_data"] = H(data)
    out["ctr_ct"] = H(enc.update(data) + enc.finalize())
    # ---- bcrypt_pbkdf, from the `bcrypt` package (OpenSSH's algorithm,
    # its own implementation). Lengths across the 32-byte stride, a binary
    # salt, and the 48 bytes an aes256-ctr key file asks for.
    try:
        import bcrypt
    except ImportError:
        bcrypt = None
    if bcrypt is not None:
        cases = [(b"password", b"salt", 4, 32), (b"password", b"salt", 16, 48),
                 (b"correct horse", bytes(range(16)), 8, 1),
                 (b"correct horse", b"\x00\xff" * 8, 2, 33),
                 (b"\xc3\xa9t\xc3\xa9", os.urandom(16), 3, 64)]
        out["kdf"] = [{"pass": H(p), "salt": H(s), "rounds": r, "length": n,
                       "key": H(bcrypt.kdf(p, s, n, r, ignore_few_rounds=True))}
                      for (p, s, r, n) in cases]
    out.update(make_needed())
    with open(os.path.join(d, "py.json"), "w") as f:
        json.dump(out, f)


def make_needed():
    """Transward's Needed items (2.5-2.9)."""
    selfcheck()
    out = {}
    # ChaCha20: the 16-byte IV is state words 12-15. The low counter word is
    # set to wrap inside the data, so the answer says whether the counter is
    # 32 bits (IETF) or carries into the next word (OpenSSH's 64-bit one).
    key = os.urandom(32)
    nonce = os.urandom(8)
    data = os.urandom(64 * 3 + 5)
    start = 0xfffffffe
    out["cc_key"] = H(key)
    out["cc_iv"] = H(struct.pack("<Q", start) + nonce)
    out["cc_data"] = H(data)
    out["cc_djb"] = H(chacha20_djb(key, start, nonce, data))
    out["cc_ietf"] = H(chacha20_ietf(key, start, struct.pack("<L", 0) + nonce, data))
    out["poly"] = [{"key": H(k), "msg": H(m), "tag": H(poly1305(k, m))}
                   for k, m in ((os.urandom(32), os.urandom(n)) for n in (0, 1, 15, 16, 17, 100))]
    # ECDSA / ECDH: Python's keys and signatures for gBASIC to read
    out["ec"] = {}
    for name, (curve, h, size) in CURVES.items():
        a, b = ec.generate_private_key(curve), ec.generate_private_key(curve)
        msg = os.urandom(50)
        r, sg = decode_dss_signature(a.sign(msg, ec.ECDSA(h)))
        out["ec"][name] = {
            "private": H(a.private_numbers().private_value.to_bytes(size, "big")),
            "public": H(point(a)), "msg": H(msg), "r": be(r), "s": be(sg),
            "peer": H(point(b)), "peer_private": H(b.private_numbers().private_value.to_bytes(size, "big")),
            "shared": H(a.exchange(ec.ECDH(), b.public_key()))}
    # DH: Python's pow is the oracle
    out["dh"] = {}
    for group in (14, 16, 18):
        p = modp(group)
        x = random.randrange(2, p - 1)
        z = random.randrange(2, p - 1)
        y = pow(2, z, p)
        out["dh"][str(group)] = {"p": be(p), "x": be(x), "y": be(y), "z": be(z),
                                 "shared": be(pow(y, x, p))}
    # HKDF, from hmac, every hash and the edges: empty salt and info, one
    # byte, and exactly 255 times the hash's size
    out["hkdf"] = []
    for name, size in (("sha256", 32), ("sha384", 48), ("sha512", 64)):
        for salt, info, n in ((os.urandom(13), os.urandom(7), 42), (b"", b"", 1),
                              (os.urandom(80), b"", 255 * size)):
            ikm = os.urandom(22)
            out["hkdf"].append({"hash": name, "ikm": H(ikm), "salt": H(salt), "info": H(info),
                                "length": n, "okm": H(hkdf(name, ikm, salt, info, n))})
    # incremental hashing and sha384, from hashlib
    data = os.urandom(1000)
    out["hash_data"] = H(data)
    out["hash"] = {n: getattr(hashlib, n)(data).hexdigest() for n in ("sha256", "sha384", "sha512")}
    k, m = os.urandom(70), os.urandom(33)
    out["hmac384"] = {"key": H(k), "msg": H(m), "mac": hmac.new(k, m, hashlib.sha384).hexdigest()}
    return out


def check(d):
    with open(os.path.join(d, "py.json")) as f:
        py = json.load(f)
    with open(os.path.join(d, "gb.json")) as f:
        gb = json.load(f)
    with open(os.path.join(d, "gb2.json")) as f:
        gb.update(json.load(f))
    results = []

    def say(ok, what):
        results.append(ok)
        print(("ok   " if ok else "FAIL ") + what)

    raw = serialization.Encoding.Raw
    # gBASIC's X25519 public key, used by Python in a real exchange
    b = x25519.X25519PrivateKey.from_private_bytes(U(py["x_b_priv"]))
    theirs = x25519.X25519PublicKey.from_public_bytes(U(gb["x_pub"]))
    say(H(b.exchange(theirs)) == gb["x_shared_with_b"],
        "Python's exchange with gBASIC's public key gives gBASIC's secret")

    # gBASIC's signatures, verified by Python with the fixture's public key
    k = serialization.load_pem_private_key(py["pem_pkcs8"].encode(), None)
    msg = U(py["msg"])
    for name, h in (("sha256", hashes.SHA256()), ("sha512", hashes.SHA512())):
        try:
            k.public_key().verify(U(gb["sig_" + name]), msg, padding.PKCS1v15(), h)
            say(True, f"Python verifies gBASIC's rsa-{name} signature")
        except Exception as e:  # noqa: BLE001 -- any refusal is the answer
            say(False, f"Python verifies gBASIC's rsa-{name} signature ({e!r})")

    # a key gBASIC GENERATED: its PEM loads, its record is the same key, and a
    # signature it made verifies
    g = serialization.load_pem_private_key(gb["gen_pem"].encode(), None)
    gn = g.private_numbers()
    rec = gb["gen"]
    same = (be(gn.public_numbers.n) == rec["n"] and be(gn.d) == rec["d"] and
            be(gn.p) == rec["p"] and be(gn.q) == rec["q"] and be(gn.iqmp) == rec["iqmp"])
    say(same, "the PEM gBASIC wrote holds the key its record names")
    say(g.key_size == gb["gen_bits"], f"and it is {gb['gen_bits']} bits ({g.key_size})")
    try:
        g.public_key().verify(U(gb["gen_sig"]), msg, padding.PKCS1v15(), hashes.SHA256())
        say(True, "a signature from the generated key verifies in Python")
    except Exception as e:  # noqa: BLE001
        say(False, f"a signature from the generated key verifies in Python ({e!r})")
    pub = serialization.load_pem_public_key(gb["gen_pub_pem"].encode())
    say(pub.public_numbers().n == gn.public_numbers.n,
        "the public PEM gBASIC wrote is the same key")

    # AES-CTR in pieces, as gBASIC chained them, against one Python stream
    enc = Cipher(algorithms.AES(U(py["ctr_key"])), modes.CTR(U(py["ctr_iv"]))).encryptor()
    whole = enc.update(U(gb["ctr_plain"])) + enc.finalize()
    say(H(whole) == gb["ctr_chained"],
        "gBASIC's packets, chained by its returned counter, are Python's one stream")
    check_needed(py, gb, say)
    print(f"{sum(results)} of {len(results)} agree")
    return 0 if all(results) else 1


def check_needed(py, gb, say):
    # gBASIC's EC keys: Python verifies the signature and agrees a secret
    for name, (curve, h, size) in CURVES.items():
        g = gb["ec_" + name]
        pub = ec.EllipticCurvePublicKey.from_encoded_point(curve, U(g["public"]))
        try:
            pub.verify(encode_dss_signature(int(g["r"], 16), int(g["s"], 16)), U(g["msg"]),
                       ec.ECDSA(h))
            say(True, f"Python verifies gBASIC's ecdsa-sha2-nist{name} signature")
        except Exception as e:  # noqa: BLE001
            say(False, f"Python verifies gBASIC's ecdsa-sha2-nist{name} signature ({e!r})")
        peer = ec.derive_private_key(int(py["ec"][name]["peer_private"], 16), curve)
        say(H(peer.exchange(ec.ECDH(), pub)) == g["shared"],
            f"Python's {name} exchange with gBASIC's key gives gBASIC's secret")
    # gBASIC's DH key: in range, the generator's power, and Python agrees
    for group in ("14", "16"):
        d, g = py["dh"][group], gb["dh_" + group]
        p, x, y = int(d["p"], 16), int(g["private"], 16), int(g["public"], 16)
        say(2 <= x <= p - 2 and pow(2, x, p) == y,
            f"group {group}: gBASIC's public value is 2^private mod p, private in [2, p-2]")
        say(pow(y, int(d["z"], 16), p) == int(g["shared"], 16),
            f"group {group}: Python's secret with gBASIC's public value is gBASIC's")
    # a chacha20-poly1305@openssh.com packet gBASIC built from the parts,
    # opened by the RFC-written code (OpenSSH's PROTOCOL.chacha20poly1305)
    pk = gb["openssh_packet"]
    k = U(pk["key"])
    k_main, k_header = k[:32], k[32:]
    nonce = struct.pack(">Q", pk["seqnr"])
    sealed = U(pk["sealed"])
    polykey = chacha20_djb(k_main, 0, nonce, bytes(32))
    say(poly1305(polykey, sealed[:-16]) == sealed[-16:], "the OpenSSH packet's tag verifies")
    length = chacha20_djb(k_header, 0, nonce, sealed[:4])
    body = chacha20_djb(k_main, 1, nonce, sealed[4:-16])
    say(struct.unpack(">L", length)[0] == len(body) and H(body) == pk["plain"],
        "and it opens to its length and payload")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("make", "check"):
        sys.exit("usage: crypto_ssh_oracle.py make|check DIR")
    if sys.argv[1] == "make":
        make(sys.argv[2])
    else:
        sys.exit(check(sys.argv[2]))
