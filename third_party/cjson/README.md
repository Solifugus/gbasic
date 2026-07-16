# Vendored: cJSON

- **Upstream:** https://github.com/DaveGamble/cJSON
- **Version:** v1.7.18 (pinned tag; see `CJSON_VERSION_*` in `cJSON.h`)
- **License:** MIT (see `LICENSE`, retained verbatim)
- **Files:** `cJSON.c`, `cJSON.h` only — the parser/printer core, unmodified.

## Why it's here

`gbasic-lsp` (PLAN.md Phase L) speaks JSON-RPC 2.0 and must robustly parse
**untrusted** client JSON (nested `initialize` capabilities, string escapes,
`\uXXXX`, arbitrary whitespace) and emit correctly-escaped JSON. cJSON is a
single-translation-unit, permissively-licensed, widely-used implementation, so
vendoring it adds **no new system dependency** while avoiding a hand-rolled
parser's escaping/edge-case bug surface.

Only the LSP binary links cJSON. The main `gbasic` binary and `libgbasic.a` do
**not** depend on it: the `--json-diagnostics` CLI output is a hand-rolled
JSON-line emitter, so the interpreter stays dependency-free.

## Updating

Re-fetch `cJSON.c`, `cJSON.h`, `LICENSE` from the same tag path and bump the
version above. The files are intentionally kept unmodified so upstream updates
are a clean drop-in.
