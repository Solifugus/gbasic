# Changelog

All notable changes to the gBASIC VS Code extension are documented here.

## [0.1.0] - 2026-06-27

Initial release.

### Added
- Syntax highlighting (TextMate grammar) for gBASIC `.gb` / `.bas` files —
  keywords, constants, worded operators, the builtin functions, strings with
  `\n \t \\ \"` and `\u{…}` escapes, numbers, and `'` comments.
- `.gb` claimed globally; `.bas` opted in per-workspace via `files.associations`
  to avoid colliding with other BASIC dialects. `firstLine` content detection for
  `program …`, `library …`, `load … from …`, and a `' gbasic` modeline.
- Snippets: `program`, `function`, `library`, `if`, `ifelse`, `foreach`,
  `while`, `onerror`, `readfile`, `writefile`.
- `$gbasic` problem matcher that turns `parse error`/`runtime error at
  FILE:LINE:COL` output into clickable diagnostics, plus example run tasks.
- Language configuration: `'` line comments, bracket matching/closing, indent
  rules for `program`/`function`/`if`/`for`/`while … end`.
