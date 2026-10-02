# Windows: what it is for, and the order to build it in

**Status: Proposal.** A plan for the `windows-port` branch. Linux stays the
primary target and Windows lags it deliberately; this document is about making
the lag *coherent* rather than making Windows a smaller Linux.

Measured figures are marked and the command is named. Everything else is
argument.

---

## 1. Decide what Windows is FOR first, because that is what makes it plannable

"Port everything, slowly" produces nothing shippable for a year. The question to
answer first is which gBASIC a Windows user gets, and there is a good answer:

**Windows is where the business data is.** Excel and SQL Server are Windows
things, and gBASIC's strongest distinguishing features are aimed squarely at
them — `money`, `date` and `duration` as real value kinds; a spreadsheet engine
checked against 15,871 real Excel workbooks; `accounting`, `lending`, `credit`,
`deposits`, `finance`, `scoring` and `forensics`; deterministic SVG charts. None
of that is a Linux story.

So Windows gBASIC is **the business/spreadsheet/database subset**, and it is not
an apology. On that audience it is arguably a *better* fit than the Linux build,
which carries a webserver, an actor model and an AI stack that a finance
department will never touch.

What Windows is NOT for, and saying so early saves the effort: servers, actors,
and GUI. Those are Linux's.

## 2. The measured starting point is better than it sounds

```sh
grep -L 'load \(sqlite\|pg\|odbc\|...\)' stdlib/*.bas | wc -l
```

**46 of the 65 stdlib libraries need no native module at all.** And the release
tiers already measure the real number: the **lean** tier links only `libc` and
`libm`, and loads **44 of 65** libraries (the `full` tier loads 52). Measured by
loading every library with the staged binary, on the download page.

So **milestone 1 ships 44 of 65 libraries with no third-party dependency at
all.** The first Windows release is not a library-hunting exercise; it is making
the interpreter run.

## 3. What each native module is worth here

How many stdlib libraries each one unlocks, measured:

| module | unlocks | on Windows |
|---|---|---|
| `xml` | 6 — `finio_iso20022`, `finio_camt`, `finio_pain001`, `mdna`, `insiders`, `ownership` | needed by `xlsx` too |
| `webclient` | 4 — `edgar`, `mcp`, `llm`, `market` | libcurl; Schannel avoids OpenSSL |
| `gi` | 4 — `gtk`, `gtkui`, `sourceeditor`, `datagrid` | **out of scope**, see §5 |
| `sqlite` | 3 — `dbframe`, `edgar`, `screener` | single amalgamation; trivial |
| `webserver` | 1 | out of scope |
| `pg` | 1 | low value here |
| `odbc` | 1 | **see §4 — the cheap win** |

## 4. THE INVERSIONS: two things are EASIER on Windows

These are the reason the plan is not just "the Linux order, slower".

**ODBC is a Microsoft technology.** On Linux it needs unixODBC plus a driver;
on Windows the driver manager ships with the operating system. So `odbc` is
plausibly the **cheapest** database win on Windows and it is the one that
reaches SQL Server, Access and Excel-as-a-data-source — exactly the audience
§1 picked. *(Not yet verified on a Windows box: confirm `odbc32.dll` and the
driver manager are present and that `src/modules/`'s unixODBC calls map to it.
This is the single highest-value thing to check first.)*

**The reason `xlsx` and `xml` are excluded from the Linux tarballs does not
exist on Windows.** `tools/build-release-tarball.sh` excludes them because
libxml2's soname differs across distributions (`.so.2` for most of the range,
`.so.16` from 2.14) — a problem Windows does not have. Statically linked, a
Windows build can ship the spreadsheet engine that the Linux downloads cannot.
**That is the strongest single argument for a Windows build existing.**

## 5. What is deliberately out, and why saying so now is the point

- **GUI (`gi`, `gtk`, `gtkui`, `sourceeditor`, `datagrid` — 4 libraries).** GTK
  on Windows is its own project. And `gui_addressability_design.md` §0.3 has
  just decided the addressable layer should sit behind a **renderer seam**, with
  an SVG backend (`whisker`) as a candidate — so porting GTK to Windows would be
  work against a layer that may be replaced. Wait.
- **Actors.** `gb_channel_socketpair` already **refuses** on Windows rather than
  faking a channel, because `AF_UNIX`/`SOCK_SEQPACKET` has no Windows equivalent
  preserving both message boundaries and fd passing. `spawn` should raise a clear
  refusal. Tier 2 at the earliest.
- **The webserver.** Technically reachable — the event loop is all sockets and
  `WSAPoll` serves it (measured, see `windows_port_status.md` §4) — but nobody
  runs a production server on a Windows desktop, and it pulls in the whole
  socket-vs-fd hazard class for no §1 audience.
- **`ldap`.** Zero stdlib libraries depend on it, and Windows has its own
  `wldap32` API rather than OpenLDAP's.

## 6. The order

Each milestone is a thing that can ship.

**M1 — the interpreter, nothing else.** 82 errors to 0. Delivers **44 of 65**
libraries: the whole finance and business platform, `chart`, `frame`, `stats`,
`ari`, `notation`, `grid`, `consolidate`, `fake`, the reasoning stack. No
third-party dependency. This is the release that proves the product exists.

**M2 — ODBC.** Cheapest-per-value if §4 holds. SQL Server and Access reach the
§1 audience directly, and it is the Windows answer to "where is my data".

**M3 — zlib + libxml2, for `xlsx`.** The headline Windows feature, and the one
the Linux downloads cannot offer. zlib is small and gBASIC already hand-wrote
its own ZIP container, so only deflate is needed.

**M4 — sqlite.** A single amalgamation file that builds anywhere; cheap, and it
unlocks `dbframe` and `screener`. Placed after M3 only because ODBC already
answers "a database" for this audience.

**M5 — libcurl.** `webclient`/`http`/`smtp`, and with them `llm`, `mcp`,
`edgar`, `market`. Use **Schannel** for TLS rather than OpenSSL, which removes a
dependency rather than adding two.

## 7. Compatibility: four things to decide before much is written

These are the "might break some compatibility" worries, named.

**1. Line endings — MEASURED, and mostly already solved.** Matthew's framing is
the right one and sharper than my first draft's: this is a READING concern, since
gBASIC does not generally write its own source. Measured:

| | source (lexer) | `read_lines` | `read` |
|---|---|---|---|
| **LF** — Linux, modern macOS | works | 3 lines | verbatim |
| **CRLF** — Windows | **works** | **3 lines, CR stripped** | verbatim |
| **CR only** — classic Mac OS 9 and earlier | parse error | 1 line | verbatim |

So **the three conventions that matter already read correctly.** A CRLF `.bas`
file runs, and `read_lines` on a CRLF data file gives clean lines (`len` 1, not
2 — the CR is stripped, not kept). `read` is verbatim, which is correct: it
returns raw content, so a CR survives there and should.

The only gap is **CR-only**, which is classic Mac OS 9 — pre-2001 and
effectively extinct. Worth closing for completeness one day; not worth planning
around.

**THE REAL RISK IS ON THE WAY OUT, AND IT IS NOT GBASIC'S DOING.** There are
**680 byte-exact golden files** in this tree, and MSVCRT opens `stdout` in TEXT
mode by default, which translates every `\n` into `\r\n` **underneath the
program**. gBASIC would be emitting LF and Windows would be writing CRLF, and
every one of those 680 goldens would fail for a reason invisible in the source.

**Recommendation: set `stdout`/`stderr` to binary mode at startup on Windows**
(`_setmode(_fileno(stdout), _O_BINARY)`), so gBASIC writes LF on every platform.
One call, and it is the same determinism argument the whole test strategy rests
on: a language whose output depends on where it ran cannot have byte-exact
goldens. A Windows user opening a gBASIC-written file in Notepad is the smaller
problem, and Notepad has handled LF since 2018.

**2. Paths.** Drive letters, backslash separators, and a case-insensitive
filesystem. Everything in §7 of `library_distribution_design.md` and every
`{file}`/`{dir}`/`make_dir`/`real_path` call touches it — including `load`'s
beside-the-file rule, where case-insensitivity means two libraries that differ
only in case become one.

**3. No `fork`.** `process.run`/`start` need `CreateProcess`, and the two pipe
polls need overlapped I/O or `PeekNamedPipe` (two functions, not the loop —
measured). `spawn` refuses.

**4. HOW DO YOU TEST IT? And the answer is to write the suite in gBASIC.**

The gate is **158 bash suites** and Windows has no bash. PowerShell exists, but
porting 158 suites to it is a project larger than the port, and it would leave
two gates that can disagree — the failure this tree has a tripwire against in
four other places.

**The tree already has the answer and uses it everywhere.** Dozens of suites are
**self-checking `.bas` files**: `tests/equality_test.bas`,
`tests/recidx_test.bas`, `tests/stridx_test.bas` and many more each state their
own expected values and print `ok` or a `MISMATCH` naming both sides. The bash
runner around them only invokes the binary and diffs. On Windows, **the `.bas`
file is the suite** — it needs no shell at all, and it is the same file on both
platforms, so the two platforms cannot disagree about what it asserts.

So the recommendation is:

- **the cross-compile ratchet runs here** (`tools/cross-build-windows.sh`), and
  proves it compiles;
- **a native smoke suite of self-checking `.bas` files runs there**, and proves
  it *runs*;
- the driver on Windows is a handful of lines — PowerShell, or `process.run`
  from gBASIC itself, which needs nothing installed.

**And it is not optional.** §5 of `windows_port_status.md`: a socket is not a
file descriptor on Windows, so `read`/`write`/`close` on one **compiles cleanly
and fails at runtime**. No error count will ever notice. Something must RUN on
Windows, and the cheapest honest something is a `.bas` file that checks itself.

## 8. What a Windows release is allowed to be

It will load fewer libraries, refuse actors, have no GUI and no server. The
honest framing is not "incomplete" but **scoped**: a business and spreadsheet
language for Windows, with the finance platform intact and `xlsx` available
where even the Linux downloads omit it.

The release tiers are soname-asserted and glibc-specific
(`tools/build-release-tarball.sh`), so Windows needs its own tier definition and
its own floor assertion — and a Windows tarball should state which libraries it
loads, measured the same way the Linux page states 44 and 52.
