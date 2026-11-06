# gBASIC cryptography library — design

**Status:** COMPLETE (2026-06-30). All four phases shipped. Phases 1/2/4 are C
builtins (encoding/random/compare always available; hashing/HMAC/AES-GCM/Ed25519
behind `HAVE_LIBCRYPTO`), verified against Python `hashlib`/`hmac`/`cryptography`
and known vectors. Phase 3 is `stdlib/crypto.bas` — hex digests, signed cookies,
CSRF, flat-JSON decoding, JWT/HS256 (byte-for-byte identical to PyJWT), and nonce-managing
AES-GCM `encrypt`/`decrypt`. Suite: `tests/run_crypto.sh` (skips without
libcrypto).

## 1. Guiding decision — why this one breaks the "compositions in gBASIC" rule

The statistics library held a strict line: *primitives in C, compositions in
gBASIC.* Cryptography deliberately breaks it, for two reasons:

1. **gBASIC has no bitwise operators.** Numbers are IEEE doubles; there is no
   `xor`/`and`/`shift`/`rotate`. SHA-256 and AES are defined entirely in those
   terms. Emulating them with double arithmetic is both slow and a correctness
   minefield.
2. **You should not hand-roll crypto.** Timing side-channels, padding handling,
   and constant-time comparison are exactly the things a from-scratch BASIC
   implementation gets wrong. The responsible choice is to bind an audited
   library.

So the **primitives are C builtins backed by OpenSSL `libcrypto`** (already on the
system; the gBASIC site links TLS via libcurl). They sit behind a
`HAVE_LIBCRYPTO` guard exactly like the existing optional modules
(`HAVE_SQLITE3`, `HAVE_LIBCURL`, `HAVE_LIBXCRYPT`) and degrade to a clean runtime
error when OpenSSL is absent. The **compositions** — JWT, signed cookies, CSRF
helpers — still live in `stdlib/crypto.bas`, so the layering principle survives at
the level where it is safe to apply it.

**Encoding and randomness need no library.** base64 / base64url / hex,
`random_bytes` (CSPRNG via `/dev/urandom`, like the existing `secure_token`), and
constant-time `bytes_equal` are plain C and are **always available**, independent
of `HAVE_LIBCRYPTO`.

## 2. Data model

gBASIC strings are **binary-safe** (length-tracked, NUL-permitting — the Unicode
phase). Crypto therefore uses ordinary strings as byte buffers everywhere: keys,
digests, ciphertext, signatures, and random output are all strings. Outputs that
are not printable text (raw digests, ciphertext) are returned as raw bytes; wrap
them in `hex_encode` / `base64_encode` for display or transport. Out-of-domain
inputs (malformed base64, wrong key length, failed AEAD tag) return `unknown`,
never a bogus value — matching the rest of the stdlib.

C-side mechanics: read a string argument as `arg.as.string` (a `char *`) with
`string_length(arg.as.string)` bytes; build binary-safe results with
`value_string_n(buf, len)`.

## 3. Phase 1 — encoding, randomness, constant-time compare (no libcrypto)

| Builtin | Result |
|---|---|
| `base64_encode(s)` | standard base64 (with `=` padding) |
| `base64_decode(s)` | bytes, or `unknown` on invalid input |
| `base64url_encode(s)` | URL-safe alphabet (`-_`), **no** padding |
| `base64url_decode(s)` | bytes, or `unknown` |
| `hex_encode(s)` | lowercase hex |
| `hex_decode(s)` | bytes, or `unknown` (odd length / non-hex) |
| `random_bytes(n)` | `n` CSPRNG bytes (1..4096), raw |
| `bytes_equal(a, b)` | boolean, **constant-time** (length-independent compare) |

## 4. Phase 2 — hashing + HMAC (libcrypto)

| Builtin | Result (raw bytes) |
|---|---|
| `sha256(s)` | 32 bytes |
| `sha512(s)` | 64 bytes |
| `sha1(s)` | 20 bytes (legacy/compat) |
| `md5(s)` | 16 bytes (legacy/compat) |
| `hmac_sha256(key, msg)` | 32 bytes |
| `hmac_sha512(key, msg)` | 64 bytes |

Implemented via the OpenSSL `EVP` digest / `HMAC` interfaces. Raw output composes
with Phase 1 encoders (`hex_encode(sha256(x))` is the familiar hex digest).

## 4b. Phase 5 — password-based key derivation — **DONE (2026-08-25, 0.1.0-rc8)**

`pbkdf2_sha256` / `pbkdf2_sha512` (RFC 8018) and `scrypt` (RFC 7914), returning
raw key BYTES. Closes DOGFOOD 2026-08-22 item 3: the library had sha256/hmac/
aes-gcm but no KDF, so a passphrase could not become a key, and gBASIC Studio
declined to offer passphrase-protected secrets rather than ship a single-round
hash that looks like one.

Two decisions worth recording.

**The salt must be non-empty; both raise on an empty one.** RFC 8018 permits it.
It is always a mistake here — it turns a KDF into a plain iterated hash that a
precomputed table defeats — and, crucially, nothing about the result looks
different. This is the silent-weakening class, so it is refused. The cost is that
RFC 7914's first test vector (empty password *and* empty salt) is unreachable;
vectors 2 and 3 are used instead and are sufficient.

**The cost parameters are NOT floored.** A floor is the obvious safety rail and
it would have made the implementation untestable against the vectors that prove
it correct: RFC 6070 uses 1, 2 and 4096 iterations, RFC 7914 uses N=16. An
implementation verified only against itself is worth nothing for a KDF, because a
shared bug still round-trips and the key is simply weak. So the recommended
values live in `docs/reference.md` where a reader sees them, and `n` is still
validated for *shape* (a power of two greater than 1) since OpenSSL answers a
bare failure otherwise, which would read as "derivation failed" rather than "that
is not a cost".

Verified against INDEPENDENT implementations, never against gBASIC: PBKDF2
against python3 `hashlib.pbkdf2_hmac`, scrypt against RFC 7914 §12 (which
python3 `hashlib.scrypt` reproduces). `examples/crypto_kdf_test.bas`, plus twelve
negative fixtures covering the refusals — empty salt, zero/fractional/oversized
parameters, a non-power-of-two `n`, and a cost above the memory ceiling, which
FAILS rather than quietly deriving with less memory (the whole point of a
memory-hard KDF).

## 5. Phase 3 — web-token helpers (`stdlib/crypto.bas`, pure gBASIC) — **DONE (2026-06-30)**

Composed over Phases 1–2. Includes a tiny JSON decoder for flat records
(string/number/bool/null values) so JWT needs no new C dependency.

**Update 2026-08-25 (0.1.0-rc8).** The matching flat ENCODER is gone. Once
`json_encode` became a core builtin, this library's copy was unreachable by the
natural call — `json_encode(x)` resolved to the builtin, and the runtime warned
on every `load crypto` that it was doing so — and only `crypto.json_encode(x)`,
spelled qualified, reached the flat one. `jwt_encode` now preflights with
`json_encodable` (it cannot catch a raise) and calls the builtin.

The DECODER stays, deliberately, and the reason is worth stating because it is
not symmetry: it reads **attacker-supplied** token payloads, so it accepts RFC
8259 and nothing else. The core's non-raising decoder is `try_decode`, which
speaks the permissive gBASIC dialect (bare `nothing`, `unknown`, `inf`, `nan`),
so folding it in would let a crafted token carry values JSON has no syntax for.
There is no strict, non-raising, full-depth decoder in the core to fold into.

Fixed at the same time: the decoder **raised** on a malformed payload instead of
refusing it, which on attacker-supplied input is a denial of service rather than
a rejection — `{"a":inf}` reached `number("")` and ended the program, because
value dispatch falls through to a number for every character that is not `"`,
`t`, `f` or `n`. It now scans RFC 8259's number grammar.
`examples/crypto_json_hostile_test.bas`.

Because the encoder handles any depth and the decoder does not, `jwt_encode`
refuses a nested payload rather than minting a token `jwt_verify` cannot read —
a signer that outruns its own verifier is the round-trip hole rc7 closed between
`encode` and `decode`, one layer up.

- `sha256_hex(s)`, `sha512_hex(s)` — convenience hex digests.
- `jwt_encode(payload, secret)` / `jwt_verify(token, secret)` — HS256. verify
  returns the payload record or `unknown` (bad signature / structure / expired).
  The `exp` claim (Unix seconds) is enforced against the `epoch()` builtin when
  present. Integer claims serialize exactly: gBASIC's number formatting renders
  integer-valued doubles in full (no `%g` exponent), so `exp` and other integer
  claims are byte-exact for interop with other JWT systems.
- `sign_cookie(value, secret)` → `value.sig`; `verify_cookie(signed, secret)` →
  value or `unknown`. Uses `bytes_equal` for the signature check.
- `csrf_token(secret, session)` / `csrf_check(token, secret, session)`.

## 6. Phase 4 — symmetric + asymmetric (libcrypto) — **DONE (2026-06-30)**

- **AES-GCM** authenticated encryption (low-level, explicit nonce):
  - `aes_gcm_encrypt(key, nonce, plaintext, aad)` → `ciphertext || tag`
    (16-byte tag appended). Key 16/24/32 bytes; nonce 12 bytes.
  - `aes_gcm_decrypt(key, nonce, blob, aad)` → plaintext, or `unknown` on auth
    failure.
  - `crypto.encrypt(key, plaintext)` / `crypto.decrypt(key, blob)` in
    `crypto.bas` wrap these with a fresh random nonce packed into the blob.
- **Ed25519** signatures (modern, fixed-size, no parameter choices):
  - `ed25519_keypair()` → `{ public, private }` (raw bytes).
  - `ed25519_sign(private, msg)` → 64-byte signature.
  - `ed25519_verify(public, msg, sig)` → boolean.
- **RSA** sign/verify over PEM keys is a possible later add; Ed25519 covers the
  modern signing need with far less surface.

## 7. Testing

Golden-file, like the rest of the suite. Digests/HMAC verified against Python
`hashlib`/`hmac` and the `openssl` CLI; base64/hex against known vectors; AES-GCM
and Ed25519 round-trip plus tamper-detection (a flipped byte must fail). Because
outputs are binary, tests hex-encode before printing so the `.out` is stable and
cross-architecture deterministic. A dedicated runner skips cleanly when
`libcrypto` is absent (mirroring `run_sqlite.sh`).

## 8. Out of scope (for now)

X.509 / certificate handling, TLS termination (libcurl already covers client
TLS), and key-derivation beyond the existing `password_hash` (PBKDF2/scrypt/Argon2
could be added later). RSA came in with §9. Revisit per demand.

## 9. Phase 6 — SSH's primitives — **DONE (2026-11-05)**: Transward's Blocking and Needed crypto

Asked for by Transward, which is writing an SSH/SFTP client and server in
gBASIC on the net library (`transward/docs/gbasic-requests-for-revision.md`
§2). The same rule as §1: **bindings to libcrypto, not implementations**. The
code is `src/modules/crypto_ssh.c`, included into `eval.c` like the other
modules, and its suite is `tests/run_crypto_ssh.sh`.

| Builtin | SSH name | Result |
|---|---|---|
| `x25519_keypair()` | `curve25519-sha256` | `{ public, private }`, 32 bytes each |
| `x25519_public(private)` | | 32 bytes |
| `x25519_shared(private, peer)` | | 32 bytes; an all-zero secret **raises** |
| `rsa_generate(bits)` | | `{ n, e, d, p, q, iqmp }` |
| `rsa_public(key)` | | `{ n, e }` |
| `rsa_sign(key, msg, hash)` | `rsa-sha2-256`/`-512` | signature; SHA-1 refused |
| `rsa_verify(key, msg, sig, hash)` | also `ssh-rsa` | boolean |
| `rsa_from_pem(text[, passphrase])` | | key record |
| `rsa_to_pem(key)` | | PKCS#8 or SubjectPublicKeyInfo |
| `aes_ctr(key, counter, data)` | `aes128-ctr`/`aes256-ctr` | `{ data, counter }` |
| `bcrypt_pbkdf(pass, salt, rounds, length)` | `openssh-key-v1` kdf | bytes |
| `chacha20(key, iv, data)` | `chacha20-poly1305@openssh.com` | bytes |
| `poly1305(key, message)` | | 16-byte tag |
| `ec_keypair(curve)` | `ecdsa-sha2-nistp256`/`384`/`521`, `ecdh-sha2-*` | `{ curve, public, private }` |
| `ec_public(curve, private)` | | `{ curve, public }` |
| `ecdsa_sign(key, msg)` | | `{ r, s }` |
| `ecdsa_verify(key, msg, sig)` | | boolean |
| `ecdh_shared(key, peer_point)` | | the x coordinate, the curve's size |
| `dh_keypair(p, g)` | `diffie-hellman-group14/16/18-*` | `{ private, public }` |
| `dh_shared(p, private, peer)` | | bytes; a peer value ≤ 1 or ≥ p−1 **raises** |
| `hkdf(hash, ikm, salt, info, length)` | (TLS 1.3, not SSH) | bytes |
| `hash_start(alg)`, `hash_update(state, data)`, `hash_finish(state)` | the exchange hash | a state; the digest |
| `sha384(s)`, `hmac_sha384(key, msg)` | `nistp384`, `hmac-sha2-384` | bytes |

**`bcrypt_pbkdf` is the one exception to "bindings, not implementations"**, and it
is adopted rather than written: OpenSSL has no bcrypt_pbkdf, so
`src/vendor/bcrypt_pbkdf.c` is the code OpenSSH ships (ISC and BSD-3-Clause, see
LICENSING.md), with only mechanical changes. Two oracles check it. Python's
`bcrypt` agrees on five vectors, across the 32-byte stride and with a binary salt.
The end-to-end test Transward asked for is
`tests/crypto_ssh_keyfile_test.bas`: it opens Ed25519 and RSA keys that
`ssh-keygen -a N` encrypted with a passphrase, using only gBASIC (the wire format
parsed by hand, `bcrypt_pbkdf`, then `aes_ctr`). The keys inside must be the ones
in the `.pub` files ssh-keygen wrote, and openssl verifies a signature made with
the recovered RSA key. Two perturbations of the vendored code, one fewer
expansion round and linear rather than strided output, are both red.

**An RSA key is a record of big-endian byte strings**, the six parts an SSH
key blob and an `openssh-key-v1` file carry. It is a record rather than PEM
text or a handle because a protocol reads and writes exactly these fields, and
because a record crosses to an actor as it is. OpenSSL's other two CRT
exponents are computed. **A key whose parts do not belong together is
refused**, which matters because signing uses p, q and iqmp, so a wrong `d`
or a swapped p and q would produce signatures no verifier accepts, with
nothing saying why.

**SHA-1 verifies and does not sign.** `ssh-rsa` exists for legacy servers
under an explicit policy. Nothing new should be made with it, and refusing
here makes that a rule rather than advice.

**AES-CTR hands back the next counter** instead of keeping a context. SSH's
CTR is one keystream across packets. Returning the counter keeps the call a
pure function, which can cross to an actor, while sparing the program
128-bit arithmetic. Since SSH packets are whole blocks, "a partial block uses
up its block" never applies to it.

**Failures raise**, as §6's do since 0.1.0-rc9. Transward was the program that
found `unknown` from a crypto builtin used as a secret, so `x25519_shared`
raises on the low-order-point case rather than answering `unknown`. Measured,
OpenSSL 3.5 already refuses an all-zero X25519 result itself. The module's own
check is a backstop for an older libcrypto, and its perturbation leaves the
suite green here for that reason.

**Evidence.** RFC 7748 §5.2 and §6.1 and NIST SP 800-38A F.5.1/F.5.5 vectors.
Python `cryptography` keys and PEM files in every form. PKCS#1 v1.5
signatures equal to Python's **byte for byte**, since the scheme is
deterministic. Python then uses what gBASIC made: an exchange with its public
key, its signatures, its PEM, and its CTR packets chained by the returned
counter across a 64-bit carry. `ssh-keygen` writes a key gBASIC reads, and
`openssl dgst` verifies a signature gBASIC made, with a one-letter control.
A valgrind tier runs as well. Six of seven perturbations are red: PSS padding,
no pairwise check, SHA-1 signing, public parts only, counter carry dropped,
and a partial block not consumed (that one was green until a check was added).
The seventh is the zero check above.

### The Needed items (2.5–2.9)

**ChaCha20 and Poly1305 are the parts, not the AEAD.**
`chacha20-poly1305@openssh.com` is not RFC 8439's construction: OpenSSH
encrypts the packet length under a second key and takes a one-time Poly1305
key from block 0, so a program needs the raw cipher and the raw MAC. The
fixture builds that packet from the two builtins, and the oracle opens it.
`chacha20`'s 16-byte IV is the cipher's state words 12 to 15: the block counter
and then the nonce, little-endian. **The counter is 64 bits.** Measured, OpenSSL
carries the low counter word into the next, which is the original layout
OpenSSH uses (a 64-bit counter, then the 64-bit sequence number). It is not
RFC 8439's 32-bit counter, and the fixture starts the counter two blocks from
wrapping to show which of the two it is.

**An EC key is a record**, `{ curve, public, private }`. `curve` is `"p256"`,
`"p384"` or `"p521"`, `public` is the SEC1 uncompressed point SSH carries, and
`private` is the scalar, padded to the curve's size. **The hash is the curve's**
(SHA-256, SHA-384, SHA-512, RFC 5656 §6.2.1) and takes no argument, since an
argument would only be a way to make a signature no SSH peer checks. A
signature is `{ r, s }`, because SSH sends them as two mpints rather than as
DER. **A peer point is checked to lie on the curve**: a point off it is the
invalid-curve attack. Measured, OpenSSL 3.5's import already refuses such a
point, so the module's own public check is a backstop, and its perturbation is
green for that reason. A record whose scalar is not its point's is refused,
and that check is load-bearing (red).

**Classic DH takes the group as bytes.** SSH's groups are 2048 to 8192 bits and
gBASIC's numbers are doubles, which is the whole reason this is a binding. The
prime is at least 2048 bits. The private exponent is uniform in [2, p−2], and
the power is computed in constant time. **A peer value ≤ 1 or ≥ p−1 raises**
(RFC 4253 §8), since 1 and p−1 confine the secret to a subgroup of order two
at most.

**An incremental hash's state is a value, not a handle.**
`s = hash_update(s, chunk)` answers a new state, so a state copies, crosses to
an actor and needs no closing. Two continuations of one state are independent,
and finishing a state does not consume it. The state is OpenSSL's own SHA-2
context behind a tag. That is the deprecated low-level API, and it is the price
of a state that can be copied, since EVP's contexts cannot be serialized. **A
state is checked before it is used**, because it is bytes a program could alter:
its block position indexes OpenSSL's buffer. A perturbation that dropped that
check **segfaulted** on the fixture's forged state. A state belongs to the
machine that made it, and is not for storage or for another byte order.

**HKDF is OpenSSL's** (EVP_KDF), so a FIPS provider would cover it, which a
composition of `hmac_sha*` in gBASIC would not. Its length is refused past 255
times the chosen hash's size, by name.

**Evidence** (`tests/crypto_ssh_needed_test.bas`, 92 checks, plus the peer
stage). The published vectors are RFC 8439 §2.5.2 and §2.6.2, RFC 5869 A.1,
FIPS 180-2's sha384("abc") and RFC 4231 case 1. **ChaCha20, Poly1305 and HKDF are
also written in Python from the RFCs** in `tests/crypto_ssh_oracle.py`, and held
to those vectors before they are trusted, so they share no code with OpenSSL.
**DH is checked against Python's `pow`**, over groups 14, 16 and 18 that the
oracle **computes from RFC 3526's formula** rather than pastes, each confirmed to
be a safe prime. The curves are checked against Python's `cryptography`, in both
directions: Python verifies gBASIC's signatures and agrees secrets with its keys.
Hashing is checked against hashlib, with chunks across every block boundary.
Valgrind runs too. Six of seven perturbations are red: no pairwise check, a DH
peer of p−1 accepted, a peer of 1 accepted, the state's position unchecked (a
segfault), p384 hashed with SHA-256, and HKDF's salt and info swapped. The
seventh is the curve check above.

**Later** (Transward's own list): ML-KEM (2.10) and a FIPS provider (2.11).

---

End of crypto design.
