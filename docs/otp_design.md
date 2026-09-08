# `otp`: one-time passwords as a second factor

**Status:** Shipped (2026-09-08). `stdlib/otp.bas`, `tests/run_otp.sh`.
Implements RFC 4226 (HOTP) and RFC 6238 (TOTP).

## 1. Why gBASIC owns the factor

Delegating the second factor to an identity provider is the right answer for an
organisation that has one. Not every organisation does, and a capability that
only works for Active Directory shops is not a platform capability — so `otp`
is built here, and the identity *source* stays a separate decision. `ldap` can
supply the first factor, or a password table, or nothing; the second factor is
the same either way.

TOTP is the right first one to own: it needs **no delivery channel**, so there
is no gateway to pay for, no carrier to trust and nothing to be down at the
moment somebody needs to log in. Every authenticator app already speaks it.

Deliberately not built here: **SMS**, which needs a gateway and which SIM-swap
has made the weakest available factor — NIST deprecated it. And **WebAuthn /
passkeys**, which is the only phishing-resistant option and is a genuine
project: it needs ECDSA P-256 verification and a CBOR decoder, and this tree
has neither. Naming both is the point; neither is a small addition to this one.

## 2. What was missing, and what it cost

Almost nothing, which is why this is affordable. Already present: `hmac_sha256`
/ `hmac_sha512`, `sha1`, `bytes_equal` (constant time), `random_bytes` and
`secure_token` from `/dev/urandom`, `password_hash` / `password_verify`.

Two additions, both in the shape of their neighbours:

- **`hmac_sha1`** — `crypto_hmac()` is already generic over `EVP_MD *`, so this
  is one dispatch branch rather than an implementation. It is needed because
  **authenticator apps in practice ignore the algorithm parameter and use
  SHA1**; Google Authenticator always does.
- **`base32_encode` / `base32_decode`** (RFC 4648) — the encoding a TOTP secret
  is stored and displayed in, and what an `otpauth://` URI carries. Placed
  beside `base64`/`base64url`/`hex` rather than written in gBASIC, for the
  reason that family exists: it is a **decoder of untrusted input** (padding,
  case, invalid characters), and one implementation tested once beats a copy
  per caller.

## 3. The defects this library exists to prevent

None of them are cryptography, and every one ships looking correct.

**Replay is the one made structural.** A code is valid for a whole 30-second
step and can be presented repeatedly inside it. So `otp.check` **requires** the
last counter the account accepted, and **returns** the counter it matched:

```basic
r = otp.check(secret, typed, account.last_counter)
if r.ok then
    account.last_counter = r.counter      ' the caller must store it
end if
```

There is no default for that argument. A caller cannot reach the function
without saying what was last accepted — the rule `reasoning.finding` follows
for `search.width`, for the same reason: the parameter that is easiest to
forget is the one that must not be optional. Passing `0` is legal and means
"nothing has been accepted yet", which is honest at enrollment and *visible in
the source* to anyone reviewing it, unlike a hidden default.

**Skew is bounded, not generous.** Default ±1 step; every additional step
widens the brute-force window linearly, so a large value is refused rather than
honoured.

**Rate limiting is the actual break, and it is the caller's.** Six digits is
10⁶; with a ±1 window and no lockout, online brute force is hours. The library
cannot enforce it — that needs per-account state across requests, which a
gBASIC library has no way to hold — so the design says so plainly here and the
worked example carries a lockout rather than leaving it to be inferred.

**Enrollment must be confirmed.** A secret is not activated until the user has
returned a live code from it. Without that step a user who scanned nothing, or
scanned into the wrong account, is locked out on their next login.

**Recovery codes**, single-use and hashed at rest with `password_hash`. Without
them a lost phone is a support process, and a support process becomes the
bypass.

**The interoperability trap worth naming:** RFC 6238 permits SHA256 and SHA512,
and most authenticator apps ignore the parameter and compute SHA1 anyway. A
server configured for SHA256 rejects every code the user's app produces, and
the symptom is "the code is always wrong" — which reads as a clock problem.
The default is SHA1 and the other two are available for closed deployments.

## 3a. The surface

| Call | What it does |
| --- | --- |
| `otp.secret([bytes])` | a new shared secret, base32, from `/dev/urandom` (default 20 bytes, RFC 4226's recommendation) |
| `otp.uri(spec)` | the `otpauth://` URI an authenticator app scans; `{issuer, account, secret}` plus any option |
| `otp.hotp(secret, counter [, options])` | RFC 4226, the counter-based code |
| `otp.totp(secret, at [, options])` | RFC 6238, the code at an instant; `at` is epoch **seconds** |
| `otp.counter_at(at [, options])` | the step number an instant falls in — a caller storing `last_counter` has to be able to talk about it |
| `otp.check(secret, typed, last_counter, at [, options])` | verify → `{ok, counter, reason}` |
| `otp.recovery_codes([n])` | single-use codes, returned in the clear once; the caller hashes them |
| `otp.defaults()` | the option defaults in one place, so this page and the code cannot disagree |

Options: `digits` (6–8, default 6), `period` (default 30), `algorithm`
(default `sha1`), `skew` (0–10 steps, default 1), `t0` (default 0). An unknown
option is refused **by name** — a silently ignored `digets: 8` leaves a factor
weaker than the one that was asked for.

A code is returned as **text, never a number**: a code may begin with `0`, and
`072653` is not `72653`.

`at` is taken as an argument rather than read from the clock, so a caller can
compute the code for a stated moment — which is what enrollment confirmation
and every published test vector need.

**Two successful logins in the same step is refused**, and that is RFC 6238
§5.2 rather than an accident: the verifier must not accept a second attempt
with the same code. A user with two devices logging in twice inside thirty
seconds waits for the next step.

## 4. Testing

**The oracle is external and published.** RFC 4226 Appendix D and RFC 6238
Appendix B give exact codes, and the fixture asserts those rather than what we
produced — computed independently before implementation, not read off our own
output.

A trap the vectors themselves contain: **the SHA256 and SHA512 vectors use
different, longer seeds** (32 and 64 bytes, against SHA1's 20). Reusing the
SHA1 seed for all three gives wrong answers, and the correction gets applied to
the code rather than to the seed.

Tiers, self-checking rather than golden, because **every defect here produces a
plausible six-digit number** and a golden would record one as expected:

| Tier | What it establishes |
|---|---|
| VECTORS | RFC 4226 and RFC 6238 codes, all three algorithms, 6 and 8 digits |
| BASE32 | RFC 4648 vectors both ways, plus a decode of invalid input refused |
| REPLAY | the same code accepted then **refused**, with the control that the *next* step's code is accepted — either half alone passes on a library that refuses everything, or nothing |
| SKEW | ±1 accepted and ±2 refused, asserted as a difference |
| REFUSALS | each beside its nearest legal neighbour |
| VALGRIND | new C on the crypto path |

