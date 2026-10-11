# How a gBASIC download reaches its optional libraries

**Status: Proposal.** Nothing in this document is built. The measurements in it
were made 2026-10-11 in throwaway containers; the tree is unchanged.

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
| zlib | 8 | **static** | zlib licence, tiny, pure. Measured above. |
| libxml2 | 40 | **static** | 36 struct reads rule out `dlopen`; measured above. |
| sqlite3 | 22 | **static** | Public domain, ships as an amalgamation designed for embedding. Would take `dbframe`/`screener`/`edgar` *and remove* `libsqlite3.so.0`. |
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

1. **Static libxml2 + zlib in both tiers.** Measured above: +700 KB of tarball,
   `DT_NEEDED` unchanged at `libc`/`libm`, five libraries recovered, and `xlsx`
   — the single highest-value capability in the list, spreadsheets being a
   business staple — on every download. The release script's library sweep
   already in place is what proves it did what it claims.

2. **Static sqlite3.** Same argument, and it *removes* a `DT_NEEDED` entry. Takes
   `dbframe`, `screener` and `edgar` with it.

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
- **The real size cost of static sqlite3 and libcrypto on Linux.** The Windows
  build measured libcrypto at +4.8 MB stripped; Linux is probably similar and
  was not measured.
- **Whether a statically linked libxml2 behaves identically under the whole
  gate**, not just the probes above. `run_xlsx`, `run_xml_bigfile` and the finio
  suites would have to run against such a build, which needs the release
  container rather than a developer machine.
- **Whether any distribution patches libxml2 in a way we would lose.** Building
  it ourselves means shipping upstream rather than the distribution's build,
  which is the point and also a thing to know about: a security fix becomes ours
  to pick up rather than `apt`'s. That is a real, ongoing obligation and is the
  strongest argument *against* step 1.
