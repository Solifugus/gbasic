# Licensing

gBASIC is under **one** license: **Apache-2.0**. Every file says so in its own
header (`SPDX-License-Identifier: Apache-2.0`), and this page is the map.

| | License | Text |
|---|---|---|
| The language, the interpreter, and the whole standard library | **Apache-2.0** | [`LICENSE`](LICENSE) |

Copyright 2026 Matthew C. Tedder.

## The short version

**Write gBASIC programs, embed the interpreter, build a product on any of it,
ship it closed-source — nothing here restricts you.** That covers the language,
the `gbasic` binary, every C module compiled into it — including the whole xlsx
engine — and all 65 standard libraries.

There is no copyleft library in this tree, no commercial license to buy, and no
part of the standard library that reaches into your program's licensing.

## What is under Apache-2.0

Everything:

- The interpreter: `src/`, `include/`, `tools/`, `tests/`, `examples/`
- Every C module compiled into the binary, including `src/modules/xlsx.c`
  (the ZIP container, formula evaluator and recalculation engine),
  `src/modules/xml.c`, `src/modules/smtp.c`, `src/modules/ldap.c` and
  `src/modules/rowmodel.c`
- All 65 standard libraries in `stdlib/`:

  `accounting` `agent` `ari_advisor` `ari` `ari_discover` `automation`
  `chart` `consolidate` `credit` `crypto` `datagrid` `dates` `dbframe`
  `decision` `deposits` `discovery` `edgar` `estate` `fake` `filetree`
  `finance` `finio_all` `finio_bai2` `finio` `finio_camt` `finio_iso20022`
  `finio_nacha` `finio_ofx` `finio_pain001` `finio_rates` `finio_registry`
  `finio_watch` `forensics` `frame` `fundamentals` `gpdf` `gpdf_metrics`
  `grid` `gtk` `gtkui` `gui` `insiders` `insight` `lending` `llm` `mail`
  `market` `matrix` `mcp` `mdna` `nlq` `notation` `ocr` `otp` `ownership`
  `persist` `reasoning` `retrieval` `schedule` `scoring` `screener`
  `sourceeditor` `stats` `tools` `web`

## Why there is only one license

Until 2026-09-27 ten libraries were **AGPL-3.0-or-later** with a commercial
license offered beside them: the spreadsheet-to-database pipeline (`grid`,
`consolidate`, `dbframe`) and the EDGAR securities suite (`edgar`,
`fundamentals`, `forensics`, `insiders`, `ownership`, `mdna`, `screener`).
They are Apache-2.0 now, and the split is retired rather than adjusted.

**Copyleft on a library in an interpreted language's standard library points the
wrong way.** gBASIC libraries *are* source, and `load grid` combines that source
into the caller's program with no LGPL-style linking exception. So a business
that cleaned a spreadsheet with `grid` inside a web application owed its
**entire application** under the AGPL — and nothing at the `load` site said so.
The dependency direction the old page checked (no Apache file may depend on an
AGPL one) was the easy half; the hazard ran from the user's program into the
library, which is the direction only the user can see.

Two things follow from removing it, and both are the point rather than a side
effect. Contributions no longer need a CLA — see below. And a reader no longer
has to work out which half of the standard library they are in.

## Installing and redistributing

`make install` places `LICENSE`, `NOTICE` and this page under
`$PREFIX/share/doc/gbasic`. Apache-2.0 asks you to keep the license and the
`NOTICE` text with any redistribution, and to state changes you made.

### Third-party code in the Windows binary

The Linux `gbasic` links its optional libraries dynamically, from the system.
The **Windows `gbasic.exe` links them statically**, so it contains:

- curl;
- OpenSSL's libcrypto (Apache-2.0);
- libxml2;
- SQLite;
- zlib;
- TRE with libsystre;
- yescrypt;
- the mingw-w64 and GCC runtimes.

Each licence is permissive, and none is copyleft. **Each still asks that its
notice travel with the binary**, so every Windows package carries
`licenses/THIRD-PARTY-NOTICES.txt`, with the version and full licence text of
each component. `tools/make-notices.sh` generates it from the archives the
linker actually used. The build fails if one of those archives has no notice,
so the list cannot fall behind the binary. If you redistribute `gbasic.exe`,
keep that file with it.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Contributions are **inbound=outbound**:
you offer a change under Apache-2.0, the same license the file already carries,
and no separate agreement is needed. That is the Rust/Kubernetes/Go convention,
and it is possible here only because there is nothing left to sublicense.

## This is not legal advice

It is a description of intent by the copyright holder. If it matters to your
situation, read [`LICENSE`](LICENSE) and ask a lawyer.
