# How a gBASIC download reaches its optional libraries

**Status: steps 1, 2 and 3 are built; 4 onwards are proposals.** The
measurements were made 2026-10-11 in throwaway containers, and the sequence at
the end of this document records what has since shipped. Steps 4-6 are unbuilt
and each needs its own measurement before it is believed.

**Step 3 shipped by a technique this document did not list**, and §4 was added
afterwards to describe it. That is worth knowing before reading the rest: the
three techniques below were derived from how a program reaches *a library*, and
the fourth reaches *an algorithm* instead. Steps 1 and 2 did use the technique
their rows proposed; step 3 did not, and the reason is not that the analysis was
wrong but that the option was missing from it.

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

## Four techniques, and which one each library wants

*(Three when this was written. §4 was added after step 3 shipped by the one
that was missing — see the status note above.)*

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

### 4. Vendoring the algorithm — the technique this document left out

Not *the library*: the **algorithm**, from its own authors, under a licence
that permits it, compiled as part of gBASIC.

This was missing from the list above, and the omission cost step 3 its
technique. It is not static linking (which ships *the library's* objects and
inherits its licence) and it is not `dlopen`. It applies only where the thing
wanted is small, standardised and separable — and where someone upstream has
published a permissive implementation. That is rare. It happens to be true of
exactly one entry in the table below, and `third_party/yescrypt` was already
in the tree when this document was written.

Its axes are static linking's, with the licence question removed as well:

| | link time | `dlopen` | static | vendored |
| --- | --- | --- | --- | --- |
| soname varies | **fatal** | handled | **absent** | **absent** |
| library missing | **fatal** | clean refusal | **absent** | **absent** |
| struct ABI differs | caught at link | **unsafe** | **absent** | **absent** |
| library's licence | inherited | inherited | **inherited** | **not ours to inherit** |
| what it costs | nothing | a probe per symbol | the library's size | the algorithm's size, and a copy to keep updated |

The last cell is the real price and it is not small in kind: a vendored copy is
code this project is now responsible for tracking upstream. `third_party/yescrypt/README.md`
carries the version, the checksum and the update procedure for that reason.

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
machine: this is release packaging, not the build. ~~Licences are clean (libxml2
MIT, zlib the zlib licence, both fine under Apache-2.0) and the Windows build
already depends on exactly that reading.~~

**That sentence was the one real mistake in this document, and it took until
step 3 to notice.** Licence *compatibility* is indeed clean, and that is not the
only question a licence asks. See below.

### What static linking OBLIGES, which this document missed for two steps

A dynamically linked binary redistributes nothing: the library is the user's,
installed by their package manager, carrying its own notice on their machine. A
statically linked one **is a redistribution of that library**, and libxml2's MIT
licence ("this permission notice shall be included in all copies or substantial
portions") and yescrypt's BSD-2 ("redistributions in binary form must reproduce
the above copyright notice … in the documentation and/or other materials
provided with the distribution") each require their notice to travel with it.

The tarball shipped `LICENSE`, `NOTICE` and `LICENSING.md` — gBASIC's own — and
**no third-party notices at all**. What hid it is that `LICENSING.md` still
said *"The Linux `gbasic` links its optional libraries dynamically, from the
system"*, which had been true for the whole life of the file and was false from
`a0514d0` onward. The claim that made the obligation invisible was a claim about
the thing that created it.

**Nothing non-compliant was published.** The static work is committed and
unreleased, and the 0.7.0 archives predate it. Fixed in the release build,
which now writes `share/doc/gbasic/THIRD-PARTY-NOTICES.txt` from the sources it
actually compiled and refuses to publish an archive missing one.

**Who needs an entry is asked of the linker, not of a list** — a third-party
symbol *defined* in `bin/gbasic` means we carry that code, *undefined* means
the system supplies it, which is exactly the line the licences draw. That alone
is not enough, and the first draft proved it: deriving catches a component that
gains static linking with no notice, and **cannot** catch a probe symbol that is
simply wrong. The first draft probed `zlibVersion` and `sqlite3_open`, *neither
of which gBASIC calls* — and a static archive contributes only what is
referenced, so zlib's notice would have been omitted in silence. So the expected
set is declared too and the two must agree, with the dynamic libraries asserted
to stay dynamic so that a future static libcrypto cannot create a fresh
obligation unannounced.

All four licence texts are verified against the real pinned sources, and doing
that found two more defects of mine, in the same three lines, **each hidden by
a different thing**.

The SQLite dedication was extracted with an `awk` *range*, and the amalgamation
embeds the header of every file it was built from — so the range matched about
three hundred of them and produced 36 KB of the same blessing repeated. A
synthetic one-header fixture passed; only the real 9 MB file showed it. So it
takes the first match, bounded by the four-asterisk divider.

**And the first version of that bound was an interval expression, `/^\*{4,}/`,
which is correct in `gawk` and matches nothing in `mawk`.** The development
machine has gawk; `ubuntu:22.04` — the release image — provides mawk. So `exit`
never fired, the entire 9 MB amalgamation went into the notices file, **and
every check in the release script still passed**, because the component *was*
carried and the declared-vs-derived comparison was therefore satisfied. Measured
rather than reasoned: mawk matches `/^\*\*\*\*/` and not the interval.

Two things follow, and they are the lasting part of this step.

**The release container is the reference environment, not this machine.** That
is the libcurl-floor lesson arriving from the other direction — there the dev
machine's libcurl was *newer* than the floor, here its awk is *more capable* —
and both times the defect was invisible where it was written. Anything the
release script runs is running in `ubuntu:22.04`, and a shell feature that works
here proves nothing about it.

**An absence guard is not a plausibility guard.** Every check here asked
*whether* a component was carried; none asked whether what was carried made
sense, and a runaway extraction produces a file that is complete, correct at the
top, and megabytes long. The notices file is now bounded at 256 KB and 1 KB —
generous for four licences by orders of magnitude, and enough to catch a
runaway — and that bound is itself tripwired, since a bound nobody checks is one
a later edit removes in silence.

**And the comment written to explain all this was the FOURTH instance of the
backtick trap.** Documenting the mawk defect above, I wrote `` `exit` `` in a
comment *inside the container string* — where a backtick is command substitution
the host performs. The difference from the previous three is the whole argument
for mechanising it: those were found by a stray line in a log, or not at all,
and this one was caught by the tripwire added after the third, immediately and
by name. The guard worked; the author did not. I then nearly shipped it anyway
by reading only `grep`ped slices of the suite's output instead of the output —
which is its own lesson, and the reason the slice I happened to choose showing
an unexplained second failure was lucky rather than diligent. libxml2's licence filename is likewise *looked for*
rather than guessed (`Copyright`, then `COPYING`, then `LICENSE`, failing by
naming all three), because which one a tarball carries is a fact about that
tarball.

A note on how that verification went, since it is evidence about something
else: **both pinned archives first downloaded truncated**, the checksum guards
refused them, and three retries failed before the network recovered. So the
supply-chain check in step 1's own argument — *"a downloaded dependency with no
checksum is a supply chain somebody else controls"* — was observed refusing
real corrupt input rather than a hypothesis, and both pins were confirmed
correct afterwards.

**The generalisable rule**, since steps 4-6 would each repeat this: *changing
how a library is reached changes whether it is redistributed.* Static linking
and vendoring both create a notice obligation; `dlopen` and link-time linking do
not. `tools/make-notices.sh` had been applying exactly that rule on Windows
since 2026-10-06, including the build failure when a component has no notice —
so this, too, was an answer already in the tree that nobody re-asked for Linux.

## Per library

Measured symbol counts from the dev build's undefined symbols:

| library | symbols | technique | why |
| --- | --- | --- | --- |
| zlib | 8 | **static** | **Done.** zlib licence, tiny, pure. Measured above. |
| libxml2 | 40 | **static** | **Done.** 36 struct reads rule out `dlopen`; measured above. |
| sqlite3 | 22 | **static** | **Done.** Public domain, ships as an amalgamation designed for embedding. Takes `dbframe`/`screener` onto the lean tier (not `edgar`, which also needs libcurl) *and removes* `libsqlite3.so.0`. |
| libxcrypt | **2** | ~~dlopen~~ **vendored** | **Done**, and *not* by the technique this row proposed. The tree already held a permissive reimplementation of the same algorithm — `third_party/yescrypt` — written for Windows. See step 3. |
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

3. ~~**`dlopen` libxcrypt.**~~ **Shipped, and by a different technique than
   this step proposed — which is the interesting part.** Both downloads now
   carry `password_hash`, `password_verify` and `password_hash_cost`, from the
   **vendored yescrypt** (`third_party/yescrypt`, BSD-2) rather than from
   libxcrypt by `dlopen`. Measured: **+44 KB** of stripped binary, `DT_NEEDED`
   unchanged.

   **The answer was already in the tree, and this document did not look.**
   `third_party/yescrypt` was vendored eight days earlier for the Windows
   build, for a reason that has nothing to do with sonames: libxcrypt is LGPL
   and `gbasic.exe` links no LGPL code. It is the same algorithm from its own
   authors, and `src/eval.c` asks it for exactly what libxcrypt's default
   produces — `$y$j9T$`, `YESCRYPT_DEFAULTS`, 16 bytes of salt — because a
   password hash is *data*, written on one machine and checked on another. Its
   own README said **"Linux builds are unchanged"** the whole time, which was
   true and was also the reason nobody asked whether Linux wanted it.

   That is this document's own correction happening a second time. The whole
   note exists because static linking was reasoned from Windows and never
   re-asked for Linux; here a *solution* was written for Windows and never
   re-asked for Linux. The technique was missing from the list of three above
   (§§1-3) because that list was written from how a program reaches *a
   library*, and this reaches *an algorithm* instead — §4 was added afterwards
   to describe it.

   **`dlopen` was measured, not dismissed, and the measurement went against the
   expectation.** The guess was that libcrypt would be absent from a bare base
   image, which would have settled it outright. It is not:

   | | measured |
   | --- | --- |
   | `libcrypt.so.*` present, no packages installed | **8 of 8** images — ubuntu 22.04/24.04/26.04, debian 12/13, fedora 41/43, rocky 9 |
   | exports `crypt`, `crypt_r`, `crypt_ra`, `crypt_gensalt_ra` | **8 of 8**, by `readelf --dyn-syms` |
   | soname | `.so.1` on all five Debian-family, `.so.2` on all three RHEL-family — the split this note claimed, now measured |
   | libxcrypt's own default scheme | `$y$j9T$` on both ends of the build range (4.4.27 on ubuntu 22.04, 4.5.2 on fedora 41), each measured by building gBASIC against it *in* that image |

   So `dlopen` would have worked, and the written hash would have agreed. **The
   choice is not about whether it works.** Vendoring makes the soname question,
   the presence question *and* the `dlsym`-returns-NULL failure mode all
   **absent** rather than handled — the same three rows static linking won on
   for libxml2 — for 44 KB. "Present on the eight images we looked at" is the
   argument `tools/build-release-tarball.sh` already rejected when it chose to
   *assert* a link set rather than count one.

   **What it costs, stated because it is a real behavioural difference.** The
   vendored backend reads `$y$` and `$7$` and not the legacy `crypt(3)`
   schemes, so a `$6$`, `$2b$` or `$1$` hash from an existing user table is
   unreadable by a download. It is **refused by name** —
   *"this build verifies yescrypt (`$y$`) and scrypt (`$7$`) hashes; this one
   is `$6$`"* — and never answered `false`, because a `false` there says the
   *password* was wrong and locks a real user out with nothing reported.
   Migration means re-hashing on next login. A from-source build on a machine
   with libxcrypt still reads every scheme, so the two disagree about what they
   can *read* while agreeing exactly about what they *write*; that asymmetry is
   now in `docs/reference.md` under Password Hashing.

   **Three guards, because the capability vanished silently once already.**
   The release build makes the staged binary verify a hash **libxcrypt actually
   wrote** (not merely hash and re-verify its own output, which any backend
   satisfies while producing hashes nothing else can read) and asserts the
   `$6$` refusal fires by name. `tests/run_library_tiers.sh` then asserts
   structurally that a tier which disables libxcrypt **names a replacement** —
   derived, so a new tier fails there rather than shipping without password
   hashing — and that the probe has not been deleted.

   **And it found a dead entry in the build-switch signature.**
   `BUILD_SWITCH_SIG` carried `$(YESCRYPT_AVAILABLE)`, which is assigned
   nowhere, so it expanded to the empty string and tracked nothing while
   reading exactly like the thirteen live names beside it — measured
   `1|1|0|1|1|1|1|1|1|1|1|1|1||0`, with an empty field in the middle. Same
   class as the `-DSQLITE_ENABLE_JSON1` flag step 2 found retired upstream, and
   the same remedy: no exemption list, and a tripwire asserting every name in
   that line is assigned somewhere. `run_packaging.sh`'s switch-list check
   structurally could not see it (it greps `^[A-Z0-9_]+_AVAILABLE` from column
   one), and it skips without `dpkg-deb`, so the new check lives in a suite
   that never skips.

   **One thing left undone, and naming it is the honest part: the `dlopen`
   pattern is still unproven.** This step was the designated place to prove it,
   and vendoring removed the problem instead of exercising the mechanism — so
   step 4 cannot lean on it. Building a `dlopen` harness here would have been
   building a mechanism for a problem that no longer exists in order to
   rehearse for a different one; if libcurl needs the pattern proven, it should
   be proven on libcurl. The hybrid that would recover legacy-scheme reading —
   yescrypt always present, plus `dlopen` libxcrypt *only* for a hash the
   vendored backend cannot read — is feasible on the evidence above (both
   symbols present on all eight images) and is **not built**, because nothing
   has asked to verify a legacy hash.

4. **Then decide on libcurl and libcrypto** with evidence rather than this
   document's estimates. If both land, there is **one download**, dependency-free
   beyond libc/libm, carrying spreadsheets, XML, SQLite, compression, the web
   client and crypto — and the tier split stops existing.

   **Step 3 changed what this step inherits, in two ways.** It no longer
   inherits a proven `dlopen` pattern, because vendoring removed the problem
   rather than exercising the mechanism — so whichever of these needs `dlopen`
   is where it gets proven, with the `dlsym`-returns-NULL guard and a suite
   that moves the `.so` aside. And it inherits a fourth technique to consider:
   neither of these is a candidate for vendoring (libcurl is a protocol stack,
   libcrypto a cryptographic library — neither is a small separable algorithm
   with a permissive reimplementation), but *asking* the question is now part
   of the method, and three steps running have shown that the technique chosen
   first was not the technique that shipped.

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
- **Whether `dlopen` works in this codebase at all.** Still not demonstrated.
  Three steps have now shipped and none of them needed it: two chose static
  linking and one chose vendoring. The failure modes it adds — a soname list to
  maintain, `dlsym` returning NULL for one symbol of several, a hand-written
  prototype that is undefined behaviour rather than a link error — are
  therefore all still theoretical here, which is a reason to prove the pattern
  on the first library that genuinely requires it rather than to assume §2's
  description of it is right.
- **Whether a download should be able to read a legacy `crypt(3)` hash.** The
  vendored backend cannot, and refuses by name. The hybrid that would fix it
  (yescrypt always, `dlopen` libxcrypt only for a hash yescrypt cannot read) is
  feasible — measured, both symbols present on all eight base images — and is
  not built, because no consumer has asked. Recorded so that the next person to
  need it finds the measurement rather than repeating it.
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
