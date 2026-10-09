# Releasing gBASIC

Most of a release is already checked by something. This page is the **order**,
and for each step it names the gate that enforces it — so the prose never
becomes the authority. Where a step is genuinely manual, it says so.


## Before you start: what changed, and what a book has to revisit

`./tools/since-release.sh [tag]` reports the MECHANICAL half — which pinned
diagnostics and goldens moved since the last tag, split into *modified* (a
passage quoting one is now wrong) and *added* (something new to teach), plus the
docs pages touched and the commit subjects.

**It is a floor on what changed, never a ceiling**, and the reason is worth
knowing before trusting it: it sees only diagnostics a GOLDEN pins. Measured on
`v0.4.0..HEAD`, one `.err` moved while the same commit reworded the sentence
NINE module dispatchers emit — none of those nine is pinned, so none appeared.

So `CHANGELOG.md`'s **Unreleased** section stays hand-written and is kept current
as work lands rather than reconstructed at release time. "This breaks working
programs" is a judgement, and the curated note is the one a reader sees; the tool
exists to make writing it cheap, not to replace it.

## 1. Green gate

```sh
./tests/run_all.sh
```

Every suite, discovered by glob. A suite that **skipped entirely** is reported
separately from one that passed, because a green line that ran no assertions is
how a gate shrinks.

If another session is working in this tree — they often are — check
`git status` and confirm any failure is theirs before reading it as yours. Set a
failure aside and re-run the suite to prove it.

## 2. Close the changelog and bump the version

`## Unreleased` becomes `## <version> — <date>`. Lead with what a **reader of
the release** needs, not a list: what broke, what moved, and which of the
entries below change what a diagnostic *says*, since anything quoting a message
verbatim has to re-capture it.

The version is stated in **seven** places and guarded **four** ways. Bump all
seven; the gates catch a miss:

| Place | Guarded by |
|---|---|
| `src/main.c` (`printf("gBASIC …")`) | the source of truth for the three below |
| `README.md` (the `--version` code block) | `run_examples.sh` compares `--version` to it |
| `examples/gbasic_site/site.bas` | `run_docs_gate.sh` derives the wanted version from `src/main.c` |
| `examples/gbasic_site/site_postgres.bas` | same, and checked separately — the prototype and the deployed app can drift |
| `src/gbasic.manifest` (`assemblyIdentity version`) | `run_windows_suite.sh` compares it to `--version`, with the Win32 fourth component appended |
| `README.md` (the prose line) | — |
| `CLAUDE.md` | — |

A half-bumped tree cannot ship. That is the point of four independent checks.

**The manifest was the SEVENTH and this table said six until 2026-10-08.** The
Windows port added `src/gbasic.manifest` and, to its credit, the guard for it in
the same breath — which is what caught the miss: bumping the documented six left
the manifest at `0.6.0.0` and `run_windows_suite.sh` failed with
`says 0.6.0.0 but gbasic --version says 0.6.1`. A Win32 assembly version has
**four** components, so the check appends `.0` rather than comparing the strings,
and the bump is `X.Y.Z.0`.

**One version string in that list is NOT the release and must not move with
it.** `book_version()` in `examples/gbasic_site/site.bas` is the release **the
paperback was written against** — the page's own prose promises that archive
stays downloadable, and the checksum is printed on paper where it cannot be
corrected afterwards. The docs gate's check reads `version: "…"` and therefore
does not see it, which is deliberate; what that means in practice is that a
blanket `sed -i s/0.4.0/0.5.0/g` over the tree would pass every gate and break a
printed promise. **Bump the six by name, never by sweep** — the same correction
this file already carries for the blanket `rm` in step 7, and for the same
reason: the convenient command and the promise disagree, and the command wins.

`site.bas` holds **two** version strings for that reason: the release
(`version:`, guarded) and the book's pin (`book_version()`, which only changes
when a new edition is pinned, and that is not a release decision).

## 3. Commit, then tag

```sh
git tag -a vX.Y.Z -m "gBASIC X.Y.Z — <one line>"
```

## 4. Build both tiers **from a clean worktree of the tag**

Not from the working tree. `tools/build-release-tarball.sh` mounts `$PWD` and
does `cp -r /src` with **no dirty-tree check**, and other sessions work in this
repo — on 2026-09-27 an uncommitted one-line fix was live in the tree at build
time and would have shipped in a published artifact.

```sh
git worktree add --detach /tmp/rel vX.Y.Z
cd /tmp/rel
TIER=lean ./tools/build-release-tarball.sh
TIER=full ./tools/build-release-tarball.sh
```

Needs podman or docker: the binary's glibc floor is whatever it was **built**
against, and a development box is always the newest thing in the room. Each tier
asserts its own floor, its exact library set, that it runs, and that it finds its
stdlib after being moved. The build refuses rather than shipping an artifact
whose floor has risen.

`xlsx`, `xml` and `password_hash` are in **neither** tier, deliberately —
libxml2 and libxcrypt are the two libraries whose soname differs across
distributions.

## 5. Verify by running the artifact, not by reading the log

Extract it and do the thing a reader would do. Reading the build log is what
misses a script whose own header claims a library the tier excludes.

```sh
tar xzf gbasic-X.Y.Z-linux-x86_64-full.tar.gz
cd gbasic-X.Y.Z-linux-x86_64-full
env -u GBASIC_PATH bin/gbasic --version
# then a real program: a sqlite insert and query, an http fetch
```

## 6. Push

```sh
git push origin master && git push origin vX.Y.Z
```

## 7. Publish to the website — **manual, and not in this repo**

The live page is `content/projects/gbasic.md` in the **tedderland** checkout, not
`examples/gbasic_site/site.bas` here (that one is a prototype and is not what
serves). Stage the artifacts and deploy from there:

**A PUBLISHED RELEASE A BOOK IS BASED ON IS NEVER REMOVED** (decided
2026-10-02). This step used to begin `rm -f downloads/gbasic/gbasic-*`, "the
current release only" — which would have deleted the archive the page's own prose
promises: *"the book was written against gBASIC 0.3.0 and prints that archive's
checksum on paper, so 0.3.0 stays downloadable here for as long as the edition is
in print."* The instruction and the promise contradicted each other, and the
instruction would have won.

The page now declares the roles in its front matter, and the checker enforces
both directions:

```
version: 0.4.0        the current release — every check is asked of this tag
pinned:  0.3.0        archives that must stay downloadable
```

```sh
cd ../tedderland
# Remove ONLY artifacts for versions that are neither current nor pinned.
# NEVER a blanket rm: a pinned release must stay downloadable, and the checker
# refuses the deploy when one is missing.
cp ../gbasic/dist/gbasic-X.Y.Z-linux-x86_64*.tar.gz* downloads/gbasic/
$EDITOR content/projects/gbasic.md   # set `version:`, add the previous one to `pinned:`
git add -A && git commit -m "gBASIC X.Y.Z"
./deploy.sh --dry-run                               # read what it would change
./deploy.sh                                         # rsync --delete to the live host
```

`deploy.sh` runs `./check-gbasic-release.sh`, which **refuses** while the page is
dishonest about the release it names. It checks the page against **the tag**, not
against this tree — a published page describes a release and is therefore
*supposed* to lag development. So it requires that the page **declare** its
current version and any pinned ones, that every version it names be one of those
(a stale or typo'd version is still caught, which is what the old "exactly one"
rule was really protecting), that each be **tagged** (nothing advertises a release
that does not exist), that each pinned archive still be **staged** (the promise
runs both ways — that is the check that would have caught the blanket `rm`), and
then asks of the current tag: does every link into the repo resolve,
does every licence the page names appear in the SPDX headers, are the artifacts
it offers actually staged, and do its library counts match — the last measured by
loading every library with the staged binary itself.

That check exists because this repo's four doc-truth gates all stop at its
boundary, and a licence change once left the public page stating rights nobody
had. Checking against HEAD was the first attempt and was wrong: it refused a page
that was perfectly correct about 0.3.0 the moment development moved on, and since
`deploy.sh` rebuilds and rsyncs the **whole site**, that refusal would have
blocked a deploy of any of the other thirty projects while complaining about
gBASIC.

Old artifacts are **removed, not kept**: a superseded tarball that stays live is
precisely the file nobody should still be getting.

**`pinned:` MEANS A PROMISE EXISTS, NOT "RECENT"** — and this needed saying
because I got it wrong on 2026-10-03, the same day the rest of this section was
written. Staging 0.5.0, I added **0.4.0** to `pinned:` on the reasoning that a
superseded archive somebody may already be pointing at is not the file to delete
on the day it is superseded. Defensible, and not the policy above; worse, it was
**circular** — adding 0.4.0 to `pinned:` made that line the only place the page
named it, which then satisfied the checker's rule that every version named be
current-or-pinned. The pin justified itself.

The test is whether **something outside this repo promises the archive**. Today
exactly one thing does: the paperback prints 0.3.0's checksum on paper, where it
cannot be corrected. A release that was merely current last week promises
nothing. On that rule the list does not grow — which is the state the sentence
above exists to protect, since on the other rule it grows by one every release
forever.

Reverted at 0.5.1 (`pinned: 0.3.0` alone, 0.4.0's and 0.5.0's artifacts
removed). **If a grace period is wanted, it is a rule with a stated length that
belongs here and in the checker — not a judgement made per release in a commit
message.**

## 8. Manual tail

- A GitHub Release page, if wanted — the changelog section is paste-ready.
- Tell any downstream session that pins a release (the book pins one, and runs
  its examples against the **tarball's** binary rather than `PATH`, or the pin is
  decorative).
