# Editor support for gBASIC

Syntax highlighting definitions for gBASIC source files (`.bas` / `.gb`).

## Kate / KWrite / KDevelop (`kate/gbasic.xml`)

These KDE editors use the **KSyntaxHighlighting** framework, which loads custom
language definitions from a per-user directory.

### Install

```sh
./kate/install.sh
```

or manually:

```sh
mkdir -p ~/.local/share/org.kde.syntax-highlighting/syntax
cp kate/gbasic.xml ~/.local/share/org.kde.syntax-highlighting/syntax/
```

Then fully restart Kate (close all windows). Open a `.bas` or `.gb` file — it
should auto-detect as **gBASIC**. If not, set it via
*Tools → Highlighting → Sources → gBASIC*.

> Older KDE installs may use `~/.local/share/katepart5/syntax/` instead. If the
> path above doesn't take effect, try copying there as well. The install script
> writes to both.

### What it highlights

- Control/structural keywords (`program`, `function`, `if`/`then`/`else`/`end`,
  `for each … in`, `while`, `spawn`, `on error resume next`, `load … from …`, …)
- Constants (`true`, `false`, `nothing`, `unknown`) and worded operators
  (`and`, `or`, `not`)
- The ~90 builtin functions (`print`, `len`, `split`, `list_files`,
  `password_hash`, `receive`, …)
- Strings (with `\n \t \\ \"` and `\u{…}` escapes), numbers, and `'` comments

The keyword and builtin lists are derived from `src/lexer.c` and
`src/builtins.c`; if the language gains keywords/builtins, update
`kate/gbasic.xml` to match.

## VS Code (`vscode/gbasic/`)

A full extension: syntax highlighting, snippets, a `$gbasic` problem matcher that
turns gBASIC errors into clickable diagnostics, and per-workspace run tasks.
Because `.bas` is shared by many BASIC dialects, it claims `.gb` globally but
scopes `.bas` per-workspace via `files.associations` (already set in the `gbasic`
and `tedderland` repos). Install: `./vscode/gbasic/install.sh`, then reload the
window. See `vscode/gbasic/README.md` for details.

## vi / Vim / Neovim

Not yet provided. A Vim syntax file (`syntax/gbasic.vim` + an ftdetect rule for
`*.bas`/`*.gb`) would be a welcome addition — the keyword/builtin lists above can
be reused. (Note: classic `vi` without Vim has no syntax highlighting.)
