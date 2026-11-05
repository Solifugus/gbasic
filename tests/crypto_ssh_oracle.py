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
import json
import os
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa, x25519
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

H = bytes.hex
U = bytes.fromhex


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
    with open(os.path.join(d, "py.json"), "w") as f:
        json.dump(out, f)


def check(d):
    with open(os.path.join(d, "py.json")) as f:
        py = json.load(f)
    with open(os.path.join(d, "gb.json")) as f:
        gb = json.load(f)
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
    print(f"{sum(results)} of {len(results)} agree")
    return 0 if all(results) else 1


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("make", "check"):
        sys.exit("usage: crypto_ssh_oracle.py make|check DIR")
    if sys.argv[1] == "make":
        make(sys.argv[2])
    else:
        sys.exit(check(sys.argv[2]))
