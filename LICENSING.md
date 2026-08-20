# Licensing

gBASIC is under **two** licenses. Every file says which one in its own header
(`SPDX-License-Identifier`), and this page is the map.

| | License | Text |
|---|---|---|
| The language, the interpreter, and most of the standard library | **Apache-2.0** | [`LICENSE`](LICENSE) |
| The EDGAR suite and the spreadsheet-to-database layers | **AGPL-3.0-or-later** | [`LICENSE.AGPL-3.0`](LICENSE.AGPL-3.0) |

Copyright 2026 Matthew C. Tedder.

## The short version

**If you are writing gBASIC programs, or embedding the interpreter, you are
under Apache-2.0 and nothing here restricts you.** That covers the language, the
`gbasic` binary, every C module compiled into it — including the whole xlsx
engine — and 16 of the 26 standard libraries.

**Ten standard libraries are AGPL.** If you build on those and distribute your
work, or run it as a network service, the AGPL requires you to release your
source under the AGPL too. **A commercial license is available** if that does not
suit you — contact matthewct@gmail.com.

## What is under which

### Apache-2.0

- The interpreter: `src/`, `include/`, `tools/`, `tests/`, `examples/`
- **Every C module compiled into the binary**, including `src/modules/xlsx.c`
  (the ZIP container, formula evaluator and recalculation engine),
  `src/modules/xml.c` and `src/modules/rowmodel.c`
- `stdlib/`: `ari` `chart` `crypto` `datagrid` `dates` `filetree` `frame` `gtk` `gtkui`
  `gui` `llm` `matrix` `persist` `schedule` `sourceeditor` `stats`

### AGPL-3.0-or-later

- `stdlib/`: `consolidate` `dbframe` `edgar` `forensics` `fundamentals` `grid`
  `insiders` `mdna` `ownership` `screener`

Which is: the **spreadsheet-to-database pipeline** (`grid`, `consolidate`,
`dbframe`) and the **EDGAR securities-analysis suite** (the rest).

## Why the line falls where it does

**The xlsx *engine* is Apache, the xlsx *pipeline* is AGPL.** That is not a
compromise, it is a structural fact: `src/modules/xlsx.c` is `#include`d into
`src/eval.c` and compiles into the `gbasic` binary. It cannot carry a different
license without making the entire interpreter AGPL, which would defeat the point
of a permissively licensed language. So reading, writing, evaluating and
recalculating spreadsheets is Apache-2.0 and free for any use. What is AGPL is
the layer that turns messy sheets into clean, consolidated, queryable tables.

**No Apache-licensed file depends on an AGPL one.** The dependency graph was
checked before the split, and the AGPL libraries are leaves: nothing outside
that set loads any of them. The reverse direction is fine and is used — AGPL
libraries depend on Apache ones (`grid` → `frame`, `insiders` → `dates`/`frame`),
which Apache-2.0 permits, since Apache-2.0 is one-way compatible with the GPLv3
family.

**`llm.bas` is Apache**, not AGPL, even though it was built for the EDGAR suite.
It is a general chat-completion client over `webclient` and has no securities
logic in it. `mdna.bas` (AGPL) depends on it, which is the permitted direction.

## Installing and redistributing

`make install` places both license texts under `$PREFIX/share/doc/gbasic`. The
AGPL libraries are installed into the same `stdlib` directory as the Apache ones
— they are separate works distributed together, not a combined work, and each
carries its own header. Because gBASIC libraries *are* source, the AGPL's
source-availability requirement is satisfied by the installation itself.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Code contributions are not being merged
yet, pending a Contributor License Agreement — which exists precisely so that the
AGPL files can continue to be offered under a commercial license. Without a CLA,
a contribution to an AGPL file could not be included in a commercially licensed
copy.

## This is not legal advice

It is a description of intent by the copyright holder. If the boundary matters
to your situation, read the license texts and ask a lawyer.
