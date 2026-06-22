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
- App-local form decoding is possible for simple create-topic and create-reply
  forms, but complete
  percent-decoding wants a standard helper or a byte-to-character primitive.
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
