# gBASIC Dogfooding Notes

This is a running list of friction points noticed while building the gBASIC site
sample application. These are not commitments; they are candidates to consider
after the app proves the need.

## Website App Friction

- Page templates are readable with multiline strings, but interpolation such as
  `f"...{expr}..."` would reduce concatenation noise.
- `env(name)` now covers simple service configuration, but richer typed config
  patterns may still be useful as apps grow.
- Admin login now uses a username and password verified against
  `gbasic_site_users` with `password_verify()`. Admin accounts are provisioned
  at setup time from `GBASIC_SITE_ADMIN_USER` / `GBASIC_SITE_ADMIN_PASSWORD`.
  The temporary shared-token admin path has been removed.
- The deployment port can now come from `GBASIC_SITE_PORT`, with the local
  `server_port.txt` file kept as a fallback.
- The first public-form CSRF token can now come from `GBASIC_SITE_CSRF_TOKEN`,
  but it is still a shared development token. Admin session forms now use the
  per-session CSRF token stored in Postgres.
- WebServer request records now expose parsed cookies and response records can
  emit `Set-Cookie` headers, which is enough to start server-side sessions.
- `webserver.redirect(req, location[, status])` now removes boilerplate for
  post/redirect/get flows.
- The site now creates and revokes password-backed admin sessions, and
  session-backed admin forms use per-session CSRF. Remaining auth work is
  session expiry/rotation coverage and giving anonymous public posting its own
  anti-abuse story instead of the shared development CSRF token.
- Static file serving currently needs explicit routes and typed file
  references.
- HTML escaping is app-local but should probably become a standard-library
  helper.
- Complete percent-decoding works, but the correct primitive is `from_bytes`,
  not `chr`. Unicode v1 redefined `chr`/`code` as *codepoint* builtins, so
  `chr(0xC3)` returns U+00C3 and re-encodes as two UTF-8 bytes — assembling
  `%XX` escapes with `chr` silently corrupts every multi-byte sequence. The site
  decodes each escaped byte with `from_bytes([byte_value])` and relies on
  binary-safe string concatenation to reassemble UTF-8 (verified with `café`,
  `Straße`, and an emoji). DOGFOOD LESSON: a language change (codepoint `chr`)
  silently broke a shipped app whose golden test masked it (the runner did not
  fail on output mismatch); both are now fixed. A higher-level
  `url_decode`/`form_decode` standard-library helper would remove this
  byte-assembly boilerplate — and the `chr`-vs-`from_bytes` trap — from every app.
- LANGUAGE DESIGN: adding `chr`/`code` exposed that builtin names live in a flat
  global namespace and silently collide with user identifiers. Specifically, a
  builtin name cannot be used as a modifier-assignment target — `code(uppered)=
  "abc"` now fails to parse because `code(` binds as a builtin call. User
  functions can shadow builtins (see `builtin_override_test`), but modified
  lvalues cannot. Worth deciding on a policy: a documented reserved-word list,
  letting assignments shadow builtins, or namespacing builtins. Any new builtin
  is a potential breaking change for existing programs until then.
- LANGUAGE/RUNTIME: gBASIC strings are NUL-terminated C strings, so `chr(0)`
  has no representation and raises an error. A binary-safe string (length +
  bytes) would be needed for true binary data (file bytes, raw protocols,
  blob columns) and would also let `chr` cover the full 0–255 range.
- Form validation works in app code, but named validation helpers or a small
  standard pattern would reduce repetitive HTML error handling.
- Small local HTML helpers improve form readability, but larger pages still
  want a template or interpolation story before app code feels pleasant.
- Path routing is currently manual string comparison. Prefix, parameter, and
  path-segment helpers would make routes like `/forum/general` and `/topic/1`
  clearer.
- The webserver public-binding story needs explicit design before deployment.
- The app currently has separate static and Postgres-backed entry points. A
  single configurable app would be nicer once environment/configuration support
  exists.
