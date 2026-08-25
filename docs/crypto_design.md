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
TLS), key-derivation beyond the existing `password_hash` (PBKDF2/scrypt/Argon2
could be added later), and RSA. Revisit per demand.

---

End of crypto design.
