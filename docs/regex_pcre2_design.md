# Regular expressions on PCRE2

**Status:** Proposal, 2026-11-03. Matthew's decision, relayed from gBASIC
Studio: *"Move regex to PCRE2."* This page says how, what was measured first,
and what changes.

## 1. Why

Studio's find and replace: people expect `\b`, lazy `.*?`, lookaround,
backreferences and named groups, and POSIX ERE has none of them
(`reference.md`'s *Not supported* list). On Windows the engine is TRE through
libsystre, a third implementation with its own missing-`REG_STARTEND` path.

## 2. What stays

The surface: `regex(pattern [, flags])`, `match`, `match_all`, the `contains`,
`split` and `replace` overloads, the `i`/`m`/`s` flags (`PCRE2_CASELESS`,
`PCRE2_MULTILINE`, `PCRE2_DOTALL`, now one-to-one, so the `.`/`[^...]` rewriting
in `regex_translate` goes), the match record `{text, start, length, groups}`
with **codepoint** `start` and `length`, and `unknown` for a group that did
not take part. A regex value still serializes as its pattern and flags.

**Compiled with `PCRE2_UTF | PCRE2_MATCH_INVALID_UTF`.** A gBASIC string is
bytes and may not be UTF-8; `MATCH_INVALID_UTF` (PCRE2 10.34, so above the
ubuntu 22.04 floor of 10.39) matches valid stretches and never matches across
invalid bytes, where plain `UTF` would refuse the subject. **Not `PCRE2_UCP`:**
`\d`, `\w`, `\s` and `\b` stay ASCII, as the POSIX classes were in the C
locale, so `\d` does not start matching Arabic-Indic digits silently.

**Subjects and patterns are length-counted**, so the interior-NUL refusal on a
*pattern* is lifted, and the segment-by-segment path for libcs with no
`REG_STARTEND` goes.

## 3. What changes, and the one silent one

- **New syntax works**: `\b`, `\B`, `*?`/`+?`/`??`, lookahead and lookbehind,
  `(?<name>...)`, `\1` inside a pattern, `(?i)` inline, `[\D]` inside a bracket
  (POSIX could not, so it was refused).
- **Replacement**: `$0`-`$9` and `$$` as today; `${10}` and `${name}` added, and
  `\0`-`\9` as the same references. A `$` or `\` before anything else stays
  literal, as today, so `"$5.00"` survives.
- **Named groups in the match record**: `groups` stays a list in order; a
  pattern with names also gets a `named` record. *(Decision to confirm in
  building: it is the only new field.)*
- **THE SILENT ONE: leftmost-longest becomes leftmost-first.** POSIX picks the
  longest match at the leftmost position; PCRE2 picks the first alternative
  that matches. `a|ab` on `"ab"` is `"ab"` in POSIX and `"a"` in PCRE2. This
  changes an answer with no error, so it is measured across the whole gate
  before anything is removed (§4).
- **Catastrophic backtracking raises.** A match limit (`pcre2_set_match_limit`)
  and a depth limit; past either, `regex: the pattern backtracked past ...`
  (1007) rather than a hang. `(a+)+$` on thirty `a`s and a `b` is the fixture.

## 4. Measured first, 2026-11-03

A build carrying **both engines** (in a separate clone, never committed): every
compile and every match ran through POSIX and PCRE2, each disagreement logged
with its site, pattern, flags, subject and both answers. The whole gate, which
includes the regex suite and every cookbook.

- **3,473,565 compiles, none refused by PCRE2.** No pattern in the tree is
  POSIX-only syntax.
- **3 compiles count groups differently, and POSIX is the one that is wrong.**
  `"a.b"` with `ms` and `"a[^x]b"` with `m` report one capture group under
  POSIX: the `(.|\n)` and `([^...]|\n)` that `regex_translate` inserts are
  groups, so the match record's `groups` gains an entry the pattern does not
  have. PCRE2 needs no translation, so this defect goes with it.
- **4,429,202 matches, 308 disagreeing, all at one site and one pattern:**
  `stdlib/ari.bas:109` matching a token `ari_discover.label_token` generated,
  `/BRANCH[ ]+TOTAL|TOTAL[ ]+FOR[ ]+BRANCH|BRANCH[ ]+TOTALS/`, against
  `"BRANCH TOTALS"`. Both start at the same place; POSIX ends after `TOTALS`
  and PCRE2 after `TOTAL`, the leftmost-longest case exactly. **Fixed at its
  source**, under the old engine first, since it is right under both:
  `label_token` lists the longest literal first (`run_ari_discover.sh`,
  `run_ocr.sh` and `run_ari_advisor.sh` unchanged).

So outside that generator nothing in the tree depends on leftmost-longest.

## 5. Builds

- **Linux and the BSDs:** libpcre2-8 through pkg-config, **required**, like
  bison: regex is core, so there is no `HAVE_*` guard and no compiled-out
  error. The ubuntu 22.04 floor has 10.39.
- **Release tarball:** linked **statically**, as sqlite, zlib and libxml2 are,
  so the lean tier's DT_NEEDED stays libc and libm. PCRE2 is BSD-3, so its
  notice joins the bundle (`tools/make-notices.sh`).
- **Windows (MSYS2 UCRT64):** `mingw-w64-ucrt-x86_64-pcre2`, linked statically
  with `-DPCRE2_STATIC`, replacing `-lsystre -ltre` and the gettext shim TRE
  needed.
