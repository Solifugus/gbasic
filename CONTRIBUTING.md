# Contributing to gBASIC

> ## ⚠️ Not accepting code contributions yet
>
> A Contributor License Agreement is being prepared and reviewed. **Until it is
> in place, pull requests containing code cannot be merged** — not because they
> are unwelcome, but because merging code without a signed CLA would permanently
> remove gBASIC's ability to be dual-licensed later, and that cannot be undone
> after the fact.
>
> **Issues, bug reports and questions are very welcome right now.** So are pull
> requests that are purely typo or documentation fixes, which will be held until
> the CLA lands and then merged.
>
> If you have something substantial you want to contribute, please open an issue
> first so the work is not wasted waiting.

---

gBASIC is licensed under [Apache-2.0](LICENSE). Contributions will be accepted
under the same license, plus a CLA granting the right to sublicense — see the
notice above.

## Getting set up

```sh
./tools/check-deps.sh        # says what is missing and how to install it
make                         # builds ./gbasic
make dev                     # builds everything, including gbasic-lsp
```

A C11 compiler, `make` and `bison` are all that is required. Every other
dependency is optional and compiles out cleanly when absent.

Run programs from the source tree with `GBASIC_PATH=stdlib` so `load` resolves
the standard library:

```sh
GBASIC_PATH=stdlib ./gbasic examples/xlsx_cookbook/01_open_and_look.bas
```

## Running the tests

```sh
./tests/run_examples.sh      # the main positive suite
./tests/run_negative.sh      # error and diagnostic cases
for s in tests/run_*.sh; do bash "$s" || echo "FAILED $s"; done
```

Suites that need an optional library **skip** rather than fail when it is
absent, so a minimal build should still come out clean. If a suite fails for a
missing dependency rather than skipping, that is itself a bug worth reporting.

## Writing gBASIC code

**Read [`docs/ai/START-HERE.md`](docs/ai/START-HERE.md) first**, and
[`UNLEARN.md`](docs/ai/UNLEARN.md) before that. gBASIC diverges from QBasic and
VB intuition in ways that fail *silently* — `!=` not `<>`, `find` returns
`nothing` on a miss rather than `-1`, there are no exponent literals, `load` is
an executable statement. These documents exist because each of those cost
somebody real time.

## House rules

These are the standards the codebase is held to. They are not bureaucracy; each
one exists because its absence caused a specific problem.

**Evidence over assertion.** Tests first where feasible. Measure, don't assume —
and say plainly what you could not verify. Never mark anything "verified".

**Goldens are byte-exact.** A change that moves a golden is a deliberate,
*listed* rebaseline, explained in the commit message. Goldens actively defend
bugs otherwise: three committed files here had captured broken output as
expected, so a defect was documented as a feature for weeks.

**Prove a new test red before trusting it.** A tier that has never failed may be
measuring nothing. One verification tier here passed against the very binary it
was meant to catch, because it was fed the output it was checking.

**Don't pin another project's wording.** A golden quoting a message libxml2
wrote is a golden about libxml2's *version*, and it will fail on someone else's
machine looking like a defect in your code.

**Optional dependencies stay behind their `#if HAVE_*` guard** and degrade to a
clean runtime error, never a build failure. Note that a guard is invisible to
the compiler you build with: three defects survived here for up to three weeks
because no local build compiled that configuration. CI builds the full, minimal
and installed configurations for exactly this reason.

**Adding an example test?** `tests/run_examples.sh` uses a hardcoded list, not
auto-discovery — add the filename there as well as creating the `.out`.

**Touching a design document?** Check its status line is still true. Six
documents here claimed "nothing built" for code that shipped with passing tests,
and one of them caused a working module to be reported as a release blocker.
Every document is indexed with its status in [`docs/README.md`](docs/README.md),
and the docs gate enforces that the index is complete.

**Changing an xlsx cookbook recipe?** Run `tools/sync_xlsx_cookbook.sh`. The
page's code and output blocks are checked against the real files and the suite
fails until they agree.

**Hit a limitation or a surprise?** Append an entry to [`DOGFOOD.md`](DOGFOOD.md)
using its template, before continuing. That file is the honest record of where
gBASIC is awkward, and it is more useful than any polished document.

## Commit messages

Explain *why*, not just what. The commit log here is used as a design record —
when a change prevents a specific wrong behaviour, say which one, and say how
you know. A one-line summary followed by a blank line and prose is the format.

## Reporting a bug

Include the program (as small as you can get it), what you expected, what
happened, and the output of:

```sh
./gbasic --version
./tools/check-deps.sh
```

`--tokens`, `--ast` and `--json-diagnostics` are often useful for parser
problems. If it is a crash, a build with `-fsanitize=address` usually says more
than a stack trace.
