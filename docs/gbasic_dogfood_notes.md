# gBASIC Dogfooding Notes

This is a running list of friction points noticed while building the gBASIC site
sample application. These are not commitments; they are candidates to consider
after the app proves the need.

## Website App Friction

- Page templates are readable with multiline strings, but interpolation such as
  `f"...{expr}..."` would reduce concatenation noise.
- App configuration wants an `env(name)` helper.
- The first admin token uses a local file because there is no runtime
  environment-variable helper yet. That is workable for tests, but awkward for
  deployment docs and secret handling.
- The first CSRF token is also file-backed because the runtime does not yet
  have random bytes, signed cookies, or session helpers.
- Static file serving currently needs explicit routes and typed file
  references.
- HTML escaping is app-local but should probably become a standard-library
  helper.
- App-local form decoding is possible for simple create-topic and create-reply
  forms, but complete
  percent-decoding wants a standard helper or a byte-to-character primitive.
- Form validation works in app code, but named validation helpers or a small
  standard pattern would reduce repetitive HTML error handling.
- Path routing is currently manual string comparison. Prefix, parameter, and
  path-segment helpers would make routes like `/forum/general` and `/topic/1`
  clearer.
- The webserver public-binding story needs explicit design before deployment.
- The app currently has separate static and Postgres-backed entry points. A
  single configurable app would be nicer once environment/configuration support
  exists.
