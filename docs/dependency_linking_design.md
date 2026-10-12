# How a gBASIC download reaches its optional libraries

**Status: steps 1 and 2 are built; 3 onwards are proposals.** The measurements
were made 2026-10-11 in throwaway containers, and the sequence at the end of
this document records what has since shipped. Steps 3-6 are unbuilt and each
needs its own measurement before it is believed.

## The problem, measured

Of the 65 standard libraries in the published 0.7.0 `-full` tarball, **13
cannot be `load`ed by the binary shipped beside them** — and 21 of 65 on the
lean one. Reported by the books session while writing Getting Started, and
confirmed independently against both archives: same counts, same libraries.

| cause | count | libraries |
| --- | --- | --- |
| libxml2 | 7 | `ownership` `insiders` `mdna` `finio_all` `finio_camt` `finio_iso20022` `finio_pain001` |
| GObject-Introspection | 4 | `gtk` `gtkui` `datagrid` `sourceeditor` |
| unixODBC | 1 | `discovery` |
| libpq | 1 | `retrieval` |

Plus a category the report did not reach and which is worse: **three libraries
load and then fail**, because they call a *builtin* whose availability moves
with the build. `load crypto` and `load otp` both succeed on the lean archive
and every function of either answers *HMAC requires OpenSSL*; `gpdf` needs zlib
for a compressed stream. The failure is deferred to the call, so a program gets
past its imports and fails in the middle of doing something.

**The documentation half of this is fixed** (0.7.1): the dependency is derived
from each library's own `load` lines, declared in `docs/reference.md`, gated in
both directions by `tests/run_library_tiers.sh`, and measured by
`tools/build-release-tarball.sh`, which now refuses to publish an archive whose
unloadable set is not what the derivation predicts. **This document is about the
other half** — whether the libraries have to be unloadable at all.

## Why the tiers exist

`tools/build-release-tarball.sh` ships two archives. The argument is sound and
worth restating because everything below turns on it:

> A `DT_NEEDED` entry naming a shared library the machine does not have — or
> spells differently — means the binary **does not start at all**. Not a
> degraded feature: a download that does nothing.

So every library linked into a download must be present, under that exact
soname, on the reader's machine. The lean tier links `libc` and `libm` and
nothing else; the `-full` tier adds sqlite3, libcurl, libssl, libcrypto and
zlib, whose sonames were measured identical across eight distributions.

**Two refinements this document makes to that argument.**

First, the script reasons from soname **spelling** (libxml2 is `.so.2` across
most of the range and `.so.16` from 2.14). The binding constraint is actually
**presence**: libpq, unixODBC, OpenLDAP and GTK are simply not installed on an
ordinary machine, which excludes them whatever their sonames do.

Second, the line is drawn by a judgement about how common each library is, and
that judgement is not the bright line the script presents. Measured: the
**existing `-full` archive does not start** on bare `ubuntu:22.04`,
`ubuntu:24.04` or `debian:12`, because `libsqlite3.so.0` and `libcurl.so.4` are
absent from a bare base image. Installing `curl`, `git` or `python3` brings them
in and it starts fine — so `-full` is sound for its real audience, but its
promise is "a machine with the usual runtime libraries" rather than lean's
"anywhere at or above the glibc floor", and that difference is not currently
stated anywhere.

## Three techniques, and which one each library wants

### 1. Link time — what we do now

The compiler writes the soname into `DT_NEEDED`; the loader resolves it before
`main()`. Cheapest to write, and the failure is total.

### 2. Runtime loading (`dlopen`)

The program asks for the library itself, while running, and gets function
pointers back:

```c
void *h = dlopen("libsqlite3.so.0", RTLD_NOW);
if (!h) { /* "SQLite is not available on this machine" */ }
p_sqlite3_open = dlsym(h, "sqlite3_open");
```

The binary carries no dependency and always starts. **The architecture already
thinks this way** — every optional module is behind a `HAVE_*` guard and already
degrades to a clean runtime error — so the change is *when* the question is
asked, not whether. The refusal becomes *"not available on this machine"*,
whose remedy is `apt install` rather than *"not available in this build"*, whose
remedy is rebuilding an interpreter.

**What it cannot abstract is a struct layout.** `dlsym` gives function
pointers; struct field offsets still come from the header at compile time. A
library whose layout changed between versions would be read with the wrong
offsets — not an error, a crash or silent garbage.

**And it adds a failure mode we do not have**: the library is present but too
old, so `dlsym` returns NULL for one symbol. Link time catches that for free;
with `dlopen` it must be checked per symbol or it is a null call. A wrong
hand-written prototype is likewise undefined behaviour rather than a link error,
which is a nastier class than anything in the current arrangement.

### 3. Static linking

The library's objects go **inside** the binary. No soname, no `DT_NEEDED`, no
presence question, and the struct layout is guaranteed to match the headers it
was compiled against.

**This is what the Windows build already does**, and `Makefile` says so:

> *M3: zlib + libxml2, **STATICALLY**, for xml and xlsx — the headline Windows
> feature (the Linux tarballs omit xlsx because libxml2's soname varies across
> distributions, a problem a static Windows build does not have).*

`docs/windows_port_plan.md` §4 calls that **"the strongest single argument for a
Windows build existing."** **That claim is wrong, and the correction is the
reason this document exists**: nothing about static linking is Windows-specific.
The Linux exclusion was reasoned from *dynamic* linking and then never
re-asked. Linux can do exactly the same thing, and §4 should be amended.

## The measurement that settles libxml2

Static linking beats `dlopen` here on all three axes at once:

| | link time | `dlopen` | static |
| --- | --- | --- | --- |
| soname varies | **fatal** | handled | **absent** |
| library missing | **fatal** | clean refusal | **absent** |
| struct ABI differs | caught at link | **unsafe** | **absent** |

The gBASIC XML reader does **36 struct field reads** (`node->children`,
`->next`, `->name`, `->type`), which is what makes `dlopen` unsafe for it — and
a soname bump from `.so.2` to `.so.16` is precisely the announcement that
something in the ABI changed.

**Ubuntu's own `libxml2.a` will not do.** It is built against ICU, and
`encoding.o` — which any parse needs — references `ucnv_*`, so the linker drags
in ICU, whose data alone is ~30 MB. Built from source with `--without-icu
--without-lzma --without-python --without-http --without-legacy`, libxml2's
static archive needs only `-lm -lz`.

Measured, building gBASIC on `ubuntu:22.04` at the lean tier plus static
libxml2 and static zlib:

```
baseline lean          820K   DT_NEEDED: libm libcrypt libz libc
+ static libxml2/zlib  2.2M   DT_NEEDED: libm libc
                              glibc floor GLIBC_2.34
tarball                1.5M   (from ~800K)
```

**`DT_NEEDED` is `libm` and `libc` — exactly the lean tier's existing
`EXPECTED_NEEDED`** — and `libz.so.1` *left* the list rather than joining it.
The glibc floor is 2.34, at or below the 2.35 the tier promises.

Extracted and run on bare `ubuntu:22.04` and `debian:12` with no packages
installed, both identical:

```
starts: gBASIC 0.7.0
xml  -> 1234.56 / EUR                        (parse, find, text, attr)
xlsx -> sheets 4, first cell A1=Account      (open a real workbook)
zlib -> true                                 (compress/uncompress round trip)
  ownership        ok
  finio_all        ok
  finio_camt       ok
  finio_iso20022   ok
  finio_pain001    ok
```

So: **the download that promises to run anywhere can carry spreadsheets and
XML, with no change to that promise, for about 700 KB of tarball.** Five of the
seven libxml2 libraries load; `insiders` and `mdna` still do not, on SQLite and
libcurl rather than XML, which is the correct answer for a lean build.

### What it costs

Build-side only. The release container grows a libxml2 step — download a
**pinned** tarball with a checksum, configure, make — about a minute. Nothing
changes for a gBASIC author, and nothing changes for `make` on a developer's own
machine: this is release packaging, not the build. Licences are clean (libxml2
MIT, zlib the zlib licence, both fine under Apache-2.0) and the Windows build
already depends on exactly that reading.

## Per library

Measured symbol counts from the dev build's undefined symbols:

| library | symbols | technique | why |
| --- | --- | --- | --- |
| zlib | 8 | **static** | **Done.** zlib licence, tiny, pure. Measured above. |
| libxml2 | 40 | **static** | **Done.** 36 struct reads rule out `dlopen`; measured above. |
| sqlite3 | 22 | **static** | **Done.** Public domain, ships as an amalgamation designed for embedding. Takes `dbframe`/`screener` onto the lean tier (not `edgar`, which also needs libcurl) *and removes* `libsqlite3.so.0`. |
| libxcrypt | **2** | **dlopen** | In *neither* download today, solely because the soname is `.so.1` on Debian and `.so.2` on RHEL. Two symbols. |
| libcurl | 19 | **dlopen** | Static needs a TLS backend *and* a CA-bundle path that differs per distribution (`/etc/ssl/certs/...` vs `/etc/pki/tls/certs/...`) — a runtime probe either way, so `dlopen` is the cleaner shape. |
| libcrypto | ~35 | either | Already in `-full`. Static is proven on Windows (+4.8 MB measured there). The 1.1-vs-3.x split argues for pinning to 3.x whichever route. |
| libssl | 20 | with libcurl | Serves the webserver's TLS; follows whatever libcurl does. |
| unixODBC | 24 | **dlopen** | **LGPL** — static linking obliges offering relinking. |
| libpq | 23 | dlopen | Possible, low value: one library (`retrieval`). |
| OpenLDAP | 19 | dlopen | Possible, low value, and `ldap` has never met a real directory (`ldap_design.md` §8). |
| GObject-Introspection | 47 | **neither** | See below. |

## GObject-Introspection: neither, and that is the answer

The four GUI libraries cannot be packaged by either technique, and this is not
a gap to close later.

- **`dlopen` buys nothing.** `gi` *is itself a runtime loader* — it opens
  typelibs at run time already. And the GObject type system is macros and
  structs, not an opaque-handle API.
- **Static is impossible.** GTK is **LGPL** (static linking obliges offering
  relinking), it is tens of megabytes, and decisively **it needs runtime data a
  binary cannot carry**: `.typelib` files, icon themes, GSettings schemas, a
  display server. A statically linked GTK would still refuse to start without a
  system GTK installation beside it.

So GUI is a **deployment** problem rather than a packaging one, and the right
artifact is a distribution package, not a tarball. `tools/build-deb.sh` already
exists for that, and the audience sorts itself: a GUI user is on a desktop with
a package manager by definition, while a tarball user is on a minimal server
that does not want GTK.

There is a second reason to leave it alone, and it is the project's own.
`gui_addressability_design.md` §0.3 has the addressable layer moving behind a
**renderer seam** with an SVG backend as a candidate. `windows_port_plan.md` §5
already declined to port GTK on exactly that ground — *"work against a layer
that may be replaced"* — and the same argument applies to packaging it.

## Proposed sequence

Each step is independently shippable and each is measured before the next.

1. **Static libxml2 + zlib in both tiers.** **Shipped** (`a0514d0`). Measured
   above: +700 KB of tarball, `DT_NEEDED` unchanged at `libc`/`libm`, five
   libraries recovered, and `xlsx` — the single highest-value capability in the
   list, spreadsheets being a business staple — on every download. The release
   script's library sweep already in place is what proves it did what it claims.

   **One correction to record, because that commit could not build a correct
   artifact.** The same commit made `src/builtins.c`'s registry conditional and
   gave every object a dependency on a `.buildswitch-stamp`, so that
   `has_builtin` would answer for the build it is in. `install` depends on
   `gbasic`: a `make install` invoked **without** the tier's switches therefore
   recomputed the signature from whatever `pkg-config` found, rewrote the stamp,
   rebuilt the whole tree with the wrong switches and staged *that* binary.
   Measured afterwards: a lean artifact at 832 KB carrying `libcrypt.so.1` and
   `libz.so.1`, with no xml, no sqlite and no static zlib. The two halves were
   each verified and **the combination never was** — the release measurements
   predate the stamp, and no gate builds a release tarball. Fixed by giving both
   `make` invocations one shared argument list, caught by the script's own
   link-set check, and now guarded by a source tripwire in
   `tests/run_library_tiers.sh`.

   **And fixing it produced a second defect worth more than the first.** The
   container script is a double-quoted *host* string, so a backtick inside it is
   command substitution the host performs before the container starts. The
   comment written to explain the fix above quoted `make install` in backticks
   — and the host duly ran it, with no `DESTDIR`: a full host build followed by
   an attempt to write into `/usr/local/bin`. It failed only because releases
   are not built as root. **As root it would have overwritten the system
   install as a side effect of a comment**, silently. Found by chasing one
   unexplained line in a log where every check had passed, which is the only
   reason it was found at all.

   This is the **third** instance of that trap in one file: once quoting
   `cp -r`, once in the comment written to document *that* fix, and now once in
   mine. The guard was a correct paragraph fifteen lines away, and it failed
   three times, so there is a tripwire now — no unescaped backtick anywhere in
   the container script, with its own vacuity check. The lesson is the
   project's own: a rule that must be remembered at the moment of writing is
   not a guard, and the third repetition is the evidence.

2. **Static sqlite3.** **Shipped.** Same argument, and it *removes* a
   `DT_NEEDED` entry — the full tier links two fewer shared libraries than it
   did at 0.7.0 while gaining xml and xlsx. Measured: lean 1.5 MB → **2.1 MB**
   of tarball (828 KB at 0.7.0) and 2.2 MB → **3.4 MB** of binary,
   `DT_NEEDED` still `libc`/`libm`, and the lean tier's unloadable set
   16 → **14**.

   **Two things this step corrects in the row above.** It takes `dbframe` and
   `screener` onto the lean tier but **not `edgar`**, which also needs libcurl;
   and the compile options are not a matter of taste. `PRAGMA compile_options`
   on the distribution build this replaces names 13 features, and the `sqlite`
   module hands a program **arbitrary SQL**, so an option a program can reach
   has to be matched or the static build is a *quietly reduced* one. Two were
   nearly missed by reading the list rather than classifying it:
   `LIKE_DOESNT_MATCH_BLOBS` changes what `LIKE` answers, and
   `MAX_VARIABLE_NUMBER=250000` is seven times the default. Three are C-API-only
   and unreachable from the 22 `sqlite3_*` calls in `src/eval.c`, so they are
   dropped with that reason written down; `LOAD_EXTENSION` is dropped
   deliberately and **measured behaviour-neutral**, both builds refusing
   `select load_extension(...)` because gBASIC never calls
   `sqlite3_enable_load_extension`.

   **The version jump was measured, not assumed**, since it is the real risk:
   the published archive linked ubuntu 22.04's 3.37.2 and the dev machine has
   3.46.1, so a golden relying on row order without `ORDER BY` could move under
   a different query planner. `run_sqlite`, `run_dbframe`, `run_compile`,
   `run_string_nul`, `run_xlsx_cookbook`, `run_examples` and `run_negative` all
   pass against 3.54.0 statically linked, with no golden moved.

3. **`dlopen` libxcrypt.** Two symbols, and the right place to prove the
   `dlopen` pattern — including the `dlsym`-returns-NULL guard and a suite that
   moves the `.so` aside — before committing to anything larger. Restores
   `password_hash`/`password_verify`, which are in neither download today.

4. **Then decide on libcurl and libcrypto** with evidence rather than this
   document's estimates. If both land, there is **one download**, dependency-free
   beyond libc/libm, carrying spreadsheets, XML, SQLite, compression, the web
   client and crypto — and the tier split stops existing.

5. **libpq, unixODBC, OpenLDAP**: `dlopen` if a consumer asks. Low value today.

6. **GI/GTK**: documented, and waiting on the renderer seam.

## What this document does not know

- **Whether `xmlNode`'s layout is actually identical between `.so.2` and
  `.so.16`.** If it is, `dlopen` with a soname list would also work for libxml2
  — but static linking makes the question moot, so it is recorded rather than
  investigated.
- ~~**The real size cost of static sqlite3 and libcrypto on Linux.**~~ sqlite is
  measured: **+1.2 MB of binary and +0.6 MB of tarball**, with the full option
  set above. libcrypto is still unmeasured here; the Windows build measured it
  at +4.8 MB stripped and Linux is probably similar.
- **Whether a statically linked libxml2 behaves identically under the whole
  gate**, not just the probes above. `run_xlsx`, `run_xml_bigfile` and the finio
  suites would have to run against such a build, which needs the release
  container rather than a developer machine. **Done for sqlite and not for
  libxml2**: a static sqlite was linked on the development machine and seven
  suites run against it, which is the shape the libxml2 half still wants.
- **What a distribution's sqlite enables that ours might not** — answered by
  measurement rather than left open, and the method generalises: read
  `PRAGMA compile_options` from the build being replaced, classify each option
  by whether a gBASIC program can reach it, match those and write down the
  reason for every one dropped. The same question for libcrypto (which has no
  such self-report) is harder and is the reason step 4 needs evidence.
- **Whether any distribution patches libxml2 in a way we would lose.** Building
  it ourselves means shipping upstream rather than the distribution's build,
  which is the point and also a thing to know about: a security fix becomes ours
  to pick up rather than `apt`'s. That is a real, ongoing obligation and is the
  strongest argument *against* step 1.
