# An LDAP client module

Status: **Partial** — the native module is built; the gBASIC layer above it is
deliberately not (§7).

Answers the cross-repo ask in `~/development/gdash/docs/gdash5_platform_ask_ldap.md`.

---

## 1. The ruling

**Build it, at the scope asked for and no wider.** Three things decided it.

The need is real and blocking: gdash's design §8 names LDAP(S) bind as identity
tier 2, the tier that matters for an intranet product, and the platform offers
no way to reach a directory — verified, not taken on trust: no `ldap` anywhere
in the tree, no raw TCP client, and no `ldapsearch` on the host.

The scope is genuinely small. Bind and search. Not "support LDAP".

And it is tractable, which was the open question. **`libldap` 2.6.10 is
present with headers and a `pkg-config` file**, so this is the relationship
`smtp` has with libcurl rather than a from-scratch ASN.1 BER implementation.
gdash was right that hand-rolling one would be a real project; it is not
necessary.

## 2. Scope

```text
ldap.connect(config)              -> connection
ldap.bind(connection, dn, pass)   -> { ok, reason, code, message }
ldap.search(connection, spec)     -> [ { dn, attributes } ]
ldap.close(connection)
```

Not built, and not wanted: modify, add, delete, referral chasing, SASL/GSSAPI,
pooling, paged results, schema.

## 3. `security` is required, and "plain" must be spelled

`config.security` is `"ldaps"`, `"starttls"` or `"plain"`, and **there is no
default**. Following `credit`'s delinquency method and `scoring`'s WOE
orientation: where two reasonable choices give different answers, the caller
declares.

Here the argument is sharper than usual. A default of `"plain"` would put
passwords on the wire in cleartext for anyone who omitted a field; a default of
`"ldaps"` would be safe but would mean a deployment could be secure by accident
and nobody would know which. Requiring it means **a cleartext bind is a word
somebody typed**, and is therefore greppable in a configuration review.

## 4. Verification is on, and referrals are off

`verify` defaults to **true**, and the opt-out is an explicit `verify: false` —
never implied by omitting a CA bundle. `ca_file` names an intranet CA, which is
the normal case for this module's users rather than an exotic one.

**Referral chasing is disabled and is not configurable.** A referral is the
server telling the client to go and ask a different server; on an
authentication path that is an instruction to send credentials somewhere the
operator never named. This is a security decision rather than a preference, so
there is no option to turn it back on.

## 5. Bind reports failure as a VALUE

This is the ask's central requirement and it decides the shape.

*Wrong password* and *the directory is unreachable* are both ordinary outcomes
on this path — one is shown to the user, the other is an operator's problem and
must never reach a viewer as a bad password. So `bind` does not raise for
either. It returns:

```text
{ ok: false, reason: "invalid_credentials" | "empty_password" | "unreachable"
                   | "tls_failed" | "timeout" | "server_error",
  code: <the LDAP result code>, message: <the server's diagnostic> }
```

`empty_password` is its own reason rather than folded into
`invalid_credentials`. Both fail closed, but a caller cannot otherwise tell *I
passed an empty password* — their own bug — from *the directory rejected these
credentials*.

**Every operational failure arrives by this route, in every security mode.**
That is a correction: StartTLS failure used to raise from `connect` while an
LDAPS failure answered from `bind`, so the same condition behaved differently
depending on a configuration field and the contract above held for two modes
out of three. gdash found it against a real directory and named the
consequence — a caller who guards `bind`, as this document steers them to,
takes the raise, and a raise inside a web handler kills the worker under the
let-it-crash rule, so a directory with an expired certificate would crash a
worker on **every login attempt** instead of showing "sign-in is unavailable".
It also left `tls_failed` documented but unreachable from StartTLS.

An application cannot accidentally conflate them, because it has to read
`reason` to learn anything at all. Following `market`'s treatment of a dead
network and `try_decode`'s of malformed input: failure is a value where failure
is expected.

Programmer errors still raise — a closed handle, wrong argument types, a
malformed spec. `search` also raises, because by then a bind has succeeded and
a failure is genuinely exceptional; **no results is an empty array, not an
error.**

## 6. Attribute values are always arrays

Even when a single value comes back. `memberOf` is multi-valued and `cn` is
not, and a caller that special-cases the two will be correct until the day a
user joins a second group. One shape, always.

Values are returned as gBASIC strings, which are binary-safe, so a binary
attribute survives.

## 7. What is NOT built, and why

The ask proposes — correctly — that DN construction, `memberOf` walking and
group-name normalisation live in pure gBASIC above the transport, as
`mail.bas` does above `smtp`.

**That layer is not provided here, and the omission is the ruling.** Which
attribute holds group membership, whether groups are found by reading
`memberOf` or by searching for `member`, and how a group DN normalises to an
application role are all *policy*, and gdash is the party with the
requirements. Writing them here would mean shaping that seam by reading
LDAP's requirements rather than by meeting gdash's — which is precisely the
failure gdash's own ask names, and which it would then have to work around.

The transport is the part that is genuinely ours: it is the same for everyone
and it is the part that is dangerous to get wrong.

**Two things the first caller learned, recorded here so the next one does not
have to.** Reported by gdash from a real OpenLDAP 2.6.10.

**`memberOf` is Active Directory's shape.** OpenLDAP does not populate it
unless an operator enabled the `memberof` overlay, so a caller reading only
that attribute gets **zero groups** from a stock OpenLDAP — and zero groups is
a *legitimate answer*, not an error, so it fails silently: every group-gated
page refuses everyone while the log shows successful logins. The alternative is
a search for groups whose `member` is the user's DN. Which one applies is a
property of the directory, so it belongs in configuration with **no default**,
for exactly the reason `security` has none: guessing wrong produces a plausible
answer rather than a failure.

**Always name the attributes you want.** A search with no `attributes` list
means *everything*, as LDAP defines it — and bound as an administrator that
includes `userPassword`. Correct LDAP and correct behaviour from this module,
and still a good way to put a password hash somewhere it should never be.

## 8. Validation, and its honest limit

Tested against **a mock LDAP responder** (`tests/ldap/mock_ldap.py`) that
speaks real BER over a real socket — libldap encodes and decodes against it
exactly as it would against a directory, and the mock must parse genuine
BindRequest and SearchRequest messages to answer. Verified before any of this
was built: a good password binds, a bad one returns result code 49, and a
search returns multi-valued `memberOf`.

**It has since met a real directory.** gdash ran it against **OpenLDAP 2.6.10**
in a rootless container and reported back
(`~/development/gdash/docs/gdash5_platform_report_ldap.md`). The module held:
correct bind succeeds, a wrong password is `invalid_credentials` code 49,
**a DN that does not exist answers the same way — so there is no enumeration
oracle**, an unreachable server is `unreachable`, an empty result is an empty
array, single-valued attributes still arrive as arrays, and the attribute list
is honoured. StartTLS did not downgrade against a plaintext server, LDAPS to a
plaintext port failed, and both `security: "none"` and an omitted `security`
were refused.

Three things came back and all three are fixed above: the StartTLS/LDAPS
inconsistency (§5), `empty_password` (§5), and the `memberOf` assumption (§7)
— which was wrong in *both* repositories' designs, not in the code.

**Still not tested against Active Directory**, whose `memberOf` behaviour is
the one this module's callers are most likely to rely on. And a real directory
is not every directory: referrals, aliases, size limits and controls remain
unexercised.

That limit is stated rather than papered over because this is an
authentication path, and gdash put it best: a client that is subtly wrong here
is a hole rather than a bug.
