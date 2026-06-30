# gBASIC cryptography library — design

**Status:** in progress (2026-06-30). Phase 1 (encoding + random + constant-time
compare), Phase 2 (hashing + HMAC), and Phase 4 (AES-GCM + Ed25519) are
**complete** — all C builtins, verified against Python `hashlib`/`hmac`/
`cryptography` and known vectors. Phase 3 (the gBASIC web-token helper layer over
them) is the remaining piece.

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

## 5. Phase 3 — web-token helpers (`stdlib/crypto.bas`, pure gBASIC)

Composed over Phases 1–2. Includes a tiny JSON encoder/decoder for flat records
(string/number/bool values) so JWT needs no new C dependency.

- `sha256_hex(s)`, `sha512_hex(s)` — convenience hex digests.
- `jwt_encode(payload, secret)` / `jwt_verify(token, secret)` — HS256. verify
  returns the payload record or `unknown` (bad signature / structure / `exp`).
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
TLS), key-derivation beyond the existing `password_hash` (PBKDF2/scrypt/Argon2
could be added later), and RSA. Revisit per demand.

---

End of crypto design.
