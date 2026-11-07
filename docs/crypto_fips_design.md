# FIPS mode: using OpenSSL's FIPS provider

**Status: proposal, 2026-11-07.** Asked for by Transward (its §2.11, "Later,
but design for it now"): load OpenSSL's FIPS provider, report whether it is
active, and in FIPS mode have builtins for unapproved algorithms raise a clear
error rather than run. Nothing is built. Four decisions in §6 are Matthew's.

**What this is not.** A program is not "FIPS compliant" because it ran in this
mode. Validation belongs to a specific build of a cryptographic module, and
compliance to how a whole system uses it. This mode makes gBASIC use only an
approved provider and refuse what that provider does not approve. Every
message and document says exactly that, and never claims compliance.

## 1. What gBASIC's cryptography is built on

Every crypto builtin is a binding to OpenSSL libcrypto (docs/crypto_design.md,
"primitives are bindings, not implementations"), with two exceptions that
matter here:

- **`bcrypt_pbkdf`** is OpenSSH's own code, vendored in src/vendor/ (Transward
  §2.4). No OpenSSL provider ever sees it.
- **`password_hash` / `password_verify`** use crypt(3), through libxcrypt
  where the build has it (yescrypt or bcrypt), not OpenSSL.

The machine this was written on has OpenSSL 3.5.5 with only the default and
legacy providers; there is no FIPS provider (`fips.so`) installed. So
everything below about what the provider approves is from OpenSSL's
documentation and is NOT measured here. Measuring it is §5's first step.

## 2. The builtins, by what FIPS mode does to them

From OpenSSL 3's FIPS provider as documented, to be confirmed against the
provider actually used (§5):

| builtin | in FIPS mode |
|---|---|
| `sha256`, `sha384`, `sha512`, `hash_start`/`update`/`finish` | run (approved) |
| `sha1` | runs as a digest; SHA-1 is not approved for new signatures, which no gBASIC builtin makes |
| `hmac_sha1/256/384/512` | run (approved) |
| `pbkdf2_sha256/512` | run, subject to the provider's minimums (salt length, iterations, key length); a call under them is refused by OpenSSL |
| `hkdf` | runs (EVP_KDF, approved) |
| `aes_gcm_encrypt/decrypt`, `aes_ctr` | run (approved) |
| `rsa_generate/sign/verify`, `rsa_from_pem/to_pem` | run; keys under 2048 bits refused by the provider |
| `ecdsa_sign/verify`, `ecdh_shared` on P-256, P-384, P-521 | run (approved curves) |
| `dh_keypair`, `dh_shared` | run only for the provider's named groups (the RFC 3526 / RFC 7919 safe primes); an arbitrary group is refused |
| `random_bytes` | runs, from the provider's DRBG |
| `x25519_*`, `ed25519_*` | **unknown here:** whether the provider in use approves them depends on its version (EdDSA is in FIPS 186-5, and providers have added it at different releases). Decided by asking the provider at run time (§3), not by a list in gBASIC |
| `chacha20`, `poly1305` | refused: not approved |
| `bcrypt_pbkdf` | refused by gBASIC: not OpenSSL's, so no provider can approve it |
| `password_hash`, `password_verify` | refused by gBASIC: crypt(3), not OpenSSL (pbkdf2 is the approved path) |
| `base64_*`, `hex_*`, `bytes_xor`, `bytes_equal` | run: encodings, not cryptography |

## 3. The design

1. **FIPS mode is chosen before the program runs, for the whole process.**
   `gbasic --fips program.bas`, or `GBASIC_FIPS=1` in the environment. At
   start the interpreter loads the `fips` provider, sets the default property
   query to `fips=yes`, and runs OpenSSL's self-test. A provider that cannot
   be loaded **stops the program before its first line** with a message naming
   why (no fips.so, no fipsmodule.cnf, a failed self-test). A program that
   asked for FIPS must never run without it, silently. The environment
   variable is what reaches spawned actors and child `gbasic` processes,
   which exec a new interpreter.
2. **OpenSSL decides what is approved, wherever it can.** With `fips=yes` as
   the default property, fetching an unapproved algorithm fails inside
   OpenSSL. gBASIC turns that failure into one message: "chacha20 is not
   approved by the FIPS provider, and this process runs in FIPS mode". There is
   no second list of approvals in gBASIC to drift from the provider's.
3. **gBASIC refuses what OpenSSL never sees:** `bcrypt_pbkdf` and the crypt(3)
   password builtins, by name, with the same sentence and the approved
   alternative (`pbkdf2_sha256`).
4. **A program can ask.** `crypto_fips()` answers
   `{ requested, active, provider, version }`: `active` is OpenSSL's own
   answer about the default properties, and `provider`/`version` are what
   OpenSSL reports for the loaded module, so a log can say which module did
   the work. `gbasic --capabilities` reports it too.
5. **`has_builtin` stays a question about the build**, not the mode: in FIPS
   mode `has_builtin("chacha20")` is still true, because the call exists and
   is refused for a reason the message gives. That keeps its meaning the same
   as today's ("calling it does not refuse for unavailability").

## 4. What it costs

- One startup path in main.c and one flag; the provider is loaded once.
- The refusal wording at the crypto builtins' existing OpenSSL error paths,
  which today say only that an operation failed.
- Two explicit refusals (bcrypt_pbkdf, crypt(3)).
- No change for a program that does not ask for FIPS: the default provider
  stays the default.

## 5. How it would be tested

FIPS can only be measured where a FIPS provider exists, so:

1. **A provider to test against.** Build OpenSSL's FIPS provider from source
   in a container (`./Configure enable-fips`, `openssl fipsinstall`), or use a
   distribution's FIPS build. A new tier skips, saying FIPS was not measured,
   where none is present.
2. **The provider's own answers first,** recorded before any gBASIC code: for
   each builtin's algorithm and parameters, does a fetch under `fips=yes`
   succeed. That fills in the "unknown here" rows of §2 from measurement.
3. **Each builtin both ways:** runs in FIPS mode where approved; refused, with
   the sentence, where not; and unchanged without the mode.
4. **The stops:** `--fips` with no provider, with a provider whose self-test
   fails (a corrupted fips.so in the container), and an actor of a FIPS
   process running in FIPS mode too.

## 6. Decisions for Matthew

1. **How a program asks for FIPS:** a flag and an environment variable at
   start (proposed), or also a call inside the program. A call makes the mode
   something a program can change half way, after it may already have used an
   unapproved algorithm; at start, it holds for every line.
2. **Refuse, or warn:** proposed is refuse, since FIPS mode is a promise about
   what ran.
3. **Which module is the target.** A validation belongs to a specific module
   build: OpenSSL's own validated releases, or a distribution's (RHEL, Ubuntu
   Pro). gBASIC can load any of them; which one Transward's customers will
   require decides what the test container builds.
4. **The packaging.** A FIPS-mode gBASIC needs the module beside it. Whether
   the release builds carry a FIPS variant, or document "install your
   distribution's FIPS OpenSSL", is a release decision.
