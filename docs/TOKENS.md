# gBASIC token inventory

Source of truth for external syntax highlighters and other tooling. This is a
**hand-verified** inventory derived by reading the lexer:

- `include/lexer.h` — the `TokenType` enum (every token kind).
- `src/lexer.c` — `identifier_type()` (keywords), `number_token()` /
  `string_token()` (literals), the operator/punctuation `switch` in
  `lexer_next()`, and `skip_spaces_and_comments()` (whitespace/comments).

To re-verify against a real program, dump the token stream with:

```sh
./gbasic --tokens FILE
```

Positions the lexer attaches to every token are **1-based BYTE** line/column
(the lexer has no Unicode awareness; a multi-byte UTF-8 character advances the
column by its byte count). See the `gb_span` comment in `include/diagnostics.h`.

---

## Lexing rules

- **Case:** keywords are matched **case-insensitively** (`if`, `If`, `IF` are all
  the `if` keyword). Identifiers otherwise preserve their source spelling.
- **Identifiers:** `[A-Za-z_][A-Za-z0-9_]*`, **ASCII only** — a non-ASCII byte is
  not an identifier character and lexes as an error token.
- **Whitespace:** space, tab (`\t`), and carriage return (`\r`) separate tokens
  and are otherwise ignored. A lone `\r` is **not** a line break.
- **Line breaks:** `\n` is significant and produces a `NEWLINE` token. Only `\n`
  increments the line counter.
- **Comments:** `'` (single quote) begins a comment that runs to end of line.
  There are no block comments.

---

## Keywords

Recognized case-insensitively by `identifier_type()`. Any of these as a bare word
is a keyword, not an identifier.

```
program   library   load      use       export
if        then      else      end       for
to        step      do        loop      until
while     consider  break     continue  each
function  return    print     watch     unwatch
without   watchers  modifier  goto      gosub
with      new       spawn     on        resume
next      stop      error     true      false
nothing   unknown   and       or        not
in        dim       as
```

Notes:
- `error` becomes the `ERROR_VALUE` token (used by `on error …`, `error "msg"`,
  and `error.<field>`).
- Boolean/nullish literals `true`, `false`, `nothing`, `unknown` are keywords.
- **Reserved but not accepted by the current grammar:** `dim`, `as`, `step` are
  lexed as distinct keyword tokens (`DIM`, `AS`, `STEP`) but are not wired into
  the parser, so using them is a parse error. A highlighter may still color them
  as keywords; they are reserved words. `DIM` is reserved *deliberately*, and is
  the one of the three with a message of its own: it reports that assignment
  creates a variable, which is what a reader arriving from QBasic needs told.

### Column-sensitive keyword variants

`consider` blocks are indentation-aware. When an `if` or `else` appears at the
**same column** as the enclosing `consider`, the lexer emits `CONSIDER_IF` /
`CONSIDER_ELSE` instead of `IF` / `ELSE`. Likewise `end` optionally followed by
`consider` (with spaces/tabs between) is emitted as a single `END_CONSIDER`
token. These are lexer-internal distinctions; highlighters can treat
`CONSIDER_IF`/`CONSIDER_ELSE` exactly like `if`/`else` and `END_CONSIDER` like
`end`.

---

## Literals

### Numbers (`NUMBER`)
- Decimal: `[0-9]+` with an optional fractional part `.[0-9]+`. The fractional
  part **requires** a digit after the dot, so `1.` lexes as `NUMBER 1` then `DOT`.
- Hexadecimal integer: `0x` / `0X` followed by one or more hex digits
  (`0xFF`, `0X1a`). Only when the leading digit is `0` and at least one hex digit
  follows the `x`.
- There is **no** exponent form (`1e9`), no leading-dot form (`.5`), and no sign
  (a leading `-` is the `MINUS` operator).

### Strings (`STRING`)
- Double-quoted: `"…"`. Single quotes are **not** strings (they start comments).
- A string may not span a newline (that is an "unterminated string" error).
- Valid escape sequences: `\n`, `\t`, `\\`, `\"`, and `\u{HHHH}` (Unicode scalar,
  1–6 hex digits, decoded to UTF-8 by the parser). **Any other escape is an
  error** — note `\r`, `\b`, `\f` are *not* valid gBASIC string escapes.

### Identifiers
- `IDENT` — `[A-Za-z_][A-Za-z0-9_]*` that is not a keyword.
- `QUALIFIED_IDENT` — a dotted name `name.name` **immediately followed by `(`**
  is lexed as one `QUALIFIED_IDENT` (library-qualified call, e.g. `math.max(`).
  Without the trailing `(`, `name` `.` `name` are separate `IDENT DOT IDENT`.

---

## Operators and punctuation

| Lexeme | Token        | | Lexeme | Token       |
|--------|--------------|-|--------|-------------|
| `=`    | `OP_EQ`      | | `+`    | `PLUS`      |
| `!=`   | `OP_NE`      | | `-`    | `MINUS`     |
| `>`    | `OP_GT`      | | `*`    | `STAR`      |
| `<`    | `OP_LT`      | | `/`    | `SLASH`     |
| `>=`   | `OP_GE`      | | `(`    | `LPAREN`    |
| `<=`   | `OP_LE`      | | `)`    | `RPAREN`    |
| `!>`   | `OP_NGT`     | | `[`    | `LBRACKET`  |
| `!<`   | `OP_NLT`     | | `]`    | `RBRACKET`  |
| `!>=`  | `OP_NGE`     | | `{`    | `LBRACE`    |
| `!<=`  | `OP_NLE`     | | `}`    | `RBRACE`    |
| `+=`   | `PLUS_EQ`    | | `,`    | `COMMA`     |
| `-=`   | `MINUS_EQ`   | | `.`    | `DOT`       |
| `*=`   | `STAR_EQ`    | | `:`    | `COLON`     |
| `/=`   | `SLASH_EQ`   | |        |             |

Notes:
- `=` serves as both assignment and equality (`OP_EQ`).
- The compound-assignment operators are **statement-level only** — `x += 1` is
  an assignment, not an expression, so there is no `y = (x += 1)`. Each means
  exactly `x = x op e`.
- `/=` is division-assignment here. It means "not equal" in Ada, Haskell and
  Fortran; gBASIC spells that `!=`.
- There is **no** standalone `!` token: a `!` must begin one of `!=`, `!>`, `!<`,
  `!>=`, `!<=`. A lone `!` lexes as an error token.
- The negated comparison operators (`!>` "not greater than", etc.) are a gBASIC
  extension.

---

## Context-sensitive spans

These tokens carry raw inner text captured by a stateful lexer mode; the parser
switches the mode on. Highlighters generally treat their content as a nested
expression rather than a flat token.

- `LENS_CONTENT` — the text inside a comparison lens `{ … }` (mode begun by
  `lexer_begin_lens_content`). Ends at the first `}` not inside a string; a
  newline ends the span as an error.

---

## Internal / synthetic tokens

Not produced from a specific lexeme; relevant to tooling but not to
highlighting:

- `NEWLINE` — a `\n`.
- `EOF` — end of input.
- `ERROR` — an unexpected character or malformed literal. When the lexer has a
  message (e.g. an invalid escape) it is carried alongside; otherwise the parser
  reports "unexpected token".
