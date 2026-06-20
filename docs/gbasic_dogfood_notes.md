# gBASIC Dogfooding Notes

This is a running list of friction points noticed while building the gBASIC site
sample application. These are not commitments; they are candidates to consider
after the app proves the need.

## Website App Friction

- Page templates are readable with multiline strings, but interpolation such as
  `f"...{expr}..."` would reduce concatenation noise.
- App configuration wants an `env(name)` helper.
- Static file serving currently needs explicit routes and typed file
  references.
- HTML escaping is app-local but should probably become a standard-library
  helper.
- URL/form decoding helpers will be needed before create-topic and create-reply
  forms.
- Path routing is currently manual string comparison. Prefix, parameter, and
  path-segment helpers would make routes like `/forum/general` and `/topic/1`
  clearer.
- The webserver public-binding story needs explicit design before deployment.
- The app currently has separate static and Postgres-backed entry points. A
  single configurable app would be nicer once environment/configuration support
  exists.
