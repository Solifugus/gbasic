# gBASIC for VS Code

gBASIC language support: syntax highlighting, snippets, and **errors underlined
as you type**, from the `gbasic-lsp` language server.

## Live errors (the language server)

The extension starts `gbasic-lsp` when a gBASIC file opens, and the server's
diagnostics appear as squiggles and in the Problems panel while you type --
before you save, without running anything.

- **Windows, installed from the gBASIC package (MSIX):** nothing to configure;
  the package puts `gbasic-lsp` on PATH.
- **Built from source (Linux, macOS, or a Windows build):** run
  `make gbasic-lsp`, then either put it on PATH or set **`gbasic.lsp.path`** to
  its full path. (`make install` does not install it yet.)
- **No server found:** you still get highlighting and snippets; the extension
  says so once, with a link to the setting.

The server reports parse errors today. Runtime errors still come from running
the program (see *Running & clickable errors* below).

## The `.bas` ambiguity (read this)

`.bas` is shared by many BASIC dialects (VB/VBA, QB64, FreeBASIC, …), so this
extension deliberately **does not claim `.bas` globally** — that would fight
other BASIC extensions and mislabel unrelated files. Instead it disambiguates in
three layers:

1. **`.gb` is claimed outright.** gBASIC's own extension is effectively unique, so
   `.gb` files highlight as gBASIC everywhere.
2. **`.bas` is opted in per workspace.** Add this to a project's
   `.vscode/settings.json` (already done for the `gbasic` and `tedderland`
   repos):
   ```json
   { "files.associations": { "*.bas": "gbasic" } }
   ```
   This makes `.bas` = gBASIC **only inside that workspace**, leaving other BASIC
   projects untouched. (`files.associations` outranks extension contributions, so
   it wins over any other BASIC extension you have installed.)
3. **Content heuristics** (`firstLine`) catch obvious gBASIC files anywhere — a
   file beginning with `program X(...)`, `library X`, `load X from "..."`, or a
   first-line modeline comment `' gbasic`.

You can always set the language by hand: click the language indicator in the
status bar → **gBASIC**, or *Change Language Mode* (Ctrl+K M).

## Install (local, no packaging)

```sh
./install.sh
```

Then *Developer: Reload Window*. Open a `.gb` file (or a `.bas` file inside a
workspace that has the association above).

## Install (packaged .vsix)

```sh
npm install -g @vscode/vsce
vsce package          # produces gbasic-0.1.0.vsix
code --install-extension gbasic-0.1.0.vsix
```

## Running & clickable errors

The extension contributes a `$gbasic` **problem matcher** that parses gBASIC's
diagnostics (`parse error at FILE:LINE:COL: …` / `runtime error at FILE:LINE:COL:
…`) into clickable in-editor squiggles. Each gBASIC workspace ships a
`.vscode/tasks.json` wired to it:

- **gbasic repo** — *Run current gBASIC file* (default build task, **Ctrl+Shift+B**)
  runs `./gbasic ${relativeFile}`; plus a *make* task to build the interpreter.
- **tedderland repo** — *Build site (build.bas)* (default) and *Run current
  gBASIC file*, both using `../gbasic/gbasic` with `GBASIC_PATH` set.

Run via **Ctrl+Shift+B**, or *Terminal → Run Task…*. Errors appear in the
Problems panel and as squiggles; click to jump to the line. The matcher is
reusable in your own tasks as `"problemMatcher": "$gbasic"`.

## Snippets

Type a prefix and press Tab: `program`, `function`, `library`, `if`, `ifelse`,
`foreach`, `while`, `onerror` (`on error goto next` with an `if error then` check), `readfile`,
`writefile`. These scaffold the matching `end …` so the common
"missing `end program`/`end function`" mistakes don't happen.

## Forcing keyword/literal colors

TextMate scopes pick up your theme's colors automatically (most themes already
render `keyword.control` blue/purple and strings green). To force specific colors
like the Kate setup (keywords blue, literals forest green), add to your VS Code
`settings.json`:

```json
"editor.tokenColorCustomizations": {
  "textMateRules": [
    { "scope": "keyword.control.gbasic", "settings": { "foreground": "#0000FF", "fontStyle": "bold" } },
    { "scope": ["string.quoted.double.gbasic", "constant.numeric.gbasic", "constant.language.gbasic"], "settings": { "foreground": "#228B22" } }
  ]
}
```

## Maintenance

The grammar's keyword, constant, word-operator and builtin lists are
**generated** from the interpreter (`src/lexer.c`, `src/builtins.c`): edit
`syntaxes/gbasic.tmLanguage.template.json`, then run
`python3 tools/sync_vscode_grammar.py` from the repository root.
`tests/run_editor_grammar.sh` fails when they drift, checks every snippet
parses, and asserts how real lines are coloured.

The client is TypeScript (`src/extension.ts`), bundled by esbuild into
`dist/extension.js`:

```sh
npm ci                 # once
npm run build          # dist/extension.js
npm test               # runs the extension inside a downloaded VS Code
npm run package        # gbasic-<version>.vsix
```

`tests/run_vscode_extension.sh` runs the in-VS Code test from the repository
root (it needs Node.js and `npm ci`, and skips naming which is missing).

The Kate definition (`editors/kate/gbasic.xml`) still carries hand-copied
lists and is not checked.
