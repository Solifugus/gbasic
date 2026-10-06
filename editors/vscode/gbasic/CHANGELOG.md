# Changelog

All notable changes to the gBASIC VS Code extension are documented here.

## [0.2.0] - Unreleased

### Added
- **Live errors as you type**, from the `gbasic-lsp` language server: found
  through the `gbasic.lsp.path` setting or on PATH (an installed gBASIC puts it
  there). Without a server the extension still highlights, and says so once.
- Requires VS Code 1.91 or later (the language client's floor).

### Changed
- The grammar's keyword, constant, word-operator and builtin lists are now
  GENERATED from the interpreter (`tools/sync_vscode_grammar.py`), and
  `tests/run_editor_grammar.sh` fails when they drift. They had fallen behind:
  `do`, `until` and `unwatch` were not highlighted, and `resume` (an ordinary
  name) was.
- A keyword or builtin after a dot is a field name and is no longer coloured
  (`r.count`, `rule.as`); a builtin is coloured only where it is called.
- New: modifier clauses (`{file}"path"`, `x {date}= s`, `x {split ","}= s`),
  scientific-notation and hex numbers, `server NAME(` blocks, `this` as the
  method receiver, unknown string escapes marked as invalid.
- Indentation knows `do … until`, `with … end with`, `server`, `else` bodies,
  and that an inline `if … then stmt` opens no block.
- The problem matcher also catches `lexer error at FILE:LINE:COL`.

### Fixed
- Two snippets taught retired syntax: `on error resume next` (now
  `on error goto next` with `if error then … error.clear()`), and the
  `f(file)= "path"` modifier spelling (now `f {file}= "path"`). Every snippet is
  now checked to parse.

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
