# Releasing gBASIC

Most of a release is already checked by something. This page is the **order**,
and for each step it names the gate that enforces it — so the prose never
becomes the authority. Where a step is genuinely manual, it says so.

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

The version is stated in **six** places and guarded **three** ways. Bump all
six; the gates catch a miss:

| Place | Guarded by |
|---|---|
| `src/main.c` (`printf("gBASIC …")`) | the source of truth for the two below |
| `README.md` (the `--version` code block) | `run_examples.sh` compares `--version` to it |
| `examples/gbasic_site/site.bas` | `run_docs_gate.sh` derives the wanted version from `src/main.c` |
| `examples/gbasic_site/site_postgres.bas` | same, and checked separately — the prototype and the deployed app can drift |
| `README.md` (the prose line) | — |
| `CLAUDE.md` | — |

A half-bumped tree cannot ship. That is the point of three independent checks.

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

## 8. Manual tail

- A GitHub Release page, if wanted — the changelog section is paste-ready.
- Tell any downstream session that pins a release (the book pins one, and runs
  its examples against the **tarball's** binary rather than `PATH`, or the pin is
  decorative).
