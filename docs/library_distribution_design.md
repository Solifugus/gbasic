# Library distribution: how someone else ships a gBASIC library

**Status: Proposal. None of this exists.** `load x` resolves to a file beside
the program or in the installed stdlib, and there is no manifest, no version,
no fetch and no registry.

Everything marked **measured** here was run against the tree on 2026-09-30 and
the command is named. Everything else is argument.

---

## 1. The problem

A reader finishes the book, writes something good, and has no way to publish it.
Someone else has no way to depend on it. The 65 stdlib libraries are maintained
by one person because **there is no second route**.

That is the gap that decides whether gBASIC gets an ecosystem or stays a
one-author language, and it is the one where a wrong design is hardest to undo:
naming and versioning cannot be changed once other people's code depends on
them.

## 2. Three structural advantages, measured

### 2.1 A library is SOURCE, not an artifact

No ABI, no linking, no binary compatibility — the same fact that settled the
licensing question (a gBASIC library *is* source, so `load grid` combines into
the caller's whole program). **Vendoring is therefore the natural state rather
than a workaround**, and a "build" is a file being present.

### 2.2 Two versions of one library can coexist — MEASURED

```basic
' alpha.bas          ' beta.bas
library alpha        library beta
  load util from "v1/util.bas" as am
  ...                  load util from "v2/util.bas" as bm
```

```
alpha uses util 1.0
beta  uses util 2.0
root  sees util 2.0
```

Three versions live in one program at once. `load … as` makes an alias an
**import identity** rather than a rename, which is what makes this work — it
shipped in `f21486e` for an unrelated reason.

**This is the diamond-dependency problem, and it is already solved.** npm needs
nested `node_modules` for it; Python and Maven cannot do it at all.

Without the private alias the collision is refused, with a message naming the
remedy:

> the name 'util' already refers to library 'util' from ./v1/util.bas, and
> cannot also refer to library 'util' from ./v2/util.bas — load one of them
> under another name, as in `load util from "..." as my_util`

### 2.3 The stdlib is fat, so the dependency graph is shallow

**Measured: 65 libraries, 72 total `load` edges, at most 6 direct dependencies
in any one.** Most of what drives npm's transitive explosion — a package for
padding a string — is in the box, so nobody depends on it and it cannot break.

## 3. THE DECISION: private copies, and therefore no resolution at all

Dependency hell is a consequence of **sharing one copy** of each dependency and
therefore having to *resolve* which version everyone gets. That is where version
ranges, SAT solvers, "latest compatible" and breaking minor bumps all come from.

Because §2.2 holds, gBASIC does not have to share. **Every library gets a
private copy of exactly the version it named:**

```
libs/producer/libs/c.bas      ' c 2.0.1 — producer asked for 2.0.1
libs/consumer/libs/c.bas      ' c 1.4.0 — consumer asked for 1.4.0
```

**There is no resolution step.** Nothing to negotiate, no solver, no lockfile
drift, no "minimal version selection", no upgrade that silently changes what
another library sees. A library gets what it asked for, always.

The cost is duplicate copies on disk, and for text files that is nothing. npm
pays this cost too — it just pays it *after* attempting to share and failing.

**Exact versions only. No ranges, ever.** A range makes the resolved set depend
on *when* you resolved, which is the property that breaks reproducibility. An
upgrade is an edit somebody made, never something that happened.

## 4. Identity: the name, or the origin?

**This is the decision to make before any code, and it cannot be changed later.**

Today a library's identity is the name on its `library` block, and collisions
are resolved by alias (§2.2). For *published* libraries there are two readings:

- **Identity is the declared name.** Simple, matches today, and two authors who
  both call their library `util` collide — resolvable by alias, but the
  ambiguity is real and permanent.
- **Identity is origin + version** (a URL and a version). Collisions become
  impossible **by construction** rather than by discipline, and the declared
  name becomes a local convenience.

**Recommendation: origin + version**, with the declared name kept as the default
local spelling. It costs nothing while the ecosystem is small and is unavailable
afterwards.

## 5. The one language change this needs: scope exported modifiers

**MEASURED, and it is the only place §2.2's coexistence is not real.** Two
versions of one library exporting the same modifier phrase:

```
warning: modifier 'shouted' from library 'bm' overrides modifier from library
'am' at ./a.bas:4:3 [2102]
HI!!2
HI!!2        <- `a` was written against m@1 and got m@2's modifier
```

Functions are scoped per library; **exported modifiers are global by phrase and
the last registration wins.** It warns rather than being silent, and since
2026-09-30 `on warning stop` makes that warning fatal and cannot be softened by
a dependency — but the semantics are wrong.

**A library's exported modifiers should be scoped to importers of that library,
exactly as its functions already are.** No stdlib library duplicates a modifier
phrase today, so this is latent rather than live, and the cost of fixing it is
lowest now.

## 6. What crosses the version boundary, and how it fails

Three kinds of thing pass between two coexisting versions. Measured:

| | behaviour | verdict |
|---|---|---|
| **Functions** | per-library scoping (§2.2) | versions genuinely coexist |
| **Values / records** | structural, not nominal: `unknown record field: amount`, located at the reader's line | the best available failure — loud, and it names the field |
| **Modifiers** | global by phrase, last wins, warns | §5 |

The middle row is worth dwelling on. Because gBASIC records carry **no type
identity**, a value built by `c@2` handed to code expecting `c@1` either works
(the fields match) or raises naming the missing field. There is no
`instanceof`-fails puzzle, which is exactly how this goes wrong in Java and
npm.

## 7. The mechanism, end to end

**There is no install step.** Nothing is written to a system directory, nothing
registers itself, and nothing executes — fetching a gBASIC library is
downloading a text file (trust design §1.1). "Install" is the wrong word and
that is the design, not an omission.

### 7.1 Add

```
gbasic --add https://github.com/someone/grid 2.0.1
```

Writes one line to **`gbasic.deps`**: name, **exact** version, origin, content
hash. Or edit the file — it is text, and `notation` is the natural format for it
(typed, readable, hand-editable, and already in the stdlib).

### 7.2 Fetch

```
gbasic --fetch
```

1. reads `gbasic.deps`
2. downloads each origin@version
3. **verifies the hash**, and on a mismatch refuses while printing both
4. reads *that library's own* manifest and recurses, vendoring **privately** (§3)
5. writes the tree, recording hashes on a first add

### 7.3 The layout, and why it is nested

```
gbasic.deps
libs/producer/producer.bas        <- your direct dependency
libs/producer/libs/c.bas          <- producer's private c 2.0.1, invisible to you
libs/consumer/consumer.bas
libs/consumer/libs/c.bas          <- a different c 1.4.0, also invisible
```

Each library sits in **its own directory** so that its internal
`load c from "libs/c.bas"` never has to embed its own name, and so that its
dependencies are one level BELOW it.

**That nesting is what makes private copies private, and it is enforced by a
rule that already exists.** MEASURED, both ways:

| the program does a bare `load c` | result |
|---|---|
| with `GBASIC_PATH=libs` | **reaches producer's private copy** — a dependency the program never declared, silently |
| with `GBASIC_PATH` unset | **refused**, with warning 2103: *"library 'c' at ./libs/producer/libs/c.bas was NOT used: it is below the file that loaded it, not beside it"* |

So **`GBASIC_PATH` must not be used for vendored dependencies.** The
beside-the-file rule (`run_library_depth.sh`) already gives exactly the privacy
this design needs, because `GBASIC_PATH`'s search is RECURSIVE and the
beside-the-file search is not. The 2103 warning is already the right diagnostic
for reaching at something private, and it already names the file.

### 7.4 Commit `libs/`

The project now builds forever with no network — including after the origin
disappears, which is the failure mode that produced npm's most famous outage.

### 7.5 Use, and run

```basic
load producer from "libs/producer/producer.bas"
```

The path is explicit, because that is what keeps the private copies private
(§7.3). **The interpreter learns nothing**: `load` finds a file exactly as it
does today. **Running a program touches no network and needs no tool**, which is
the property that makes dependencies "always work" — at run time there is no
dependency machinery left to fail.

### 7.6 Update and remove

Updating is editing a version and re-running `--fetch`, which **refuses if the
new version's capability set has grown** without explicit consent (trust design
§3). Removing is deleting the line and re-fetching.

## 8. What this deliberately does NOT do

**It does not build a registry.** Start with an origin URL per dependency — git,
a tarball, anything fetchable. A registry is a *discovery* convenience, and it
is where outages, typosquatting, name disputes and lock-in all live.

The vendoring/pinning/verification half provides **every guarantee** in this
document. Discovery provides none of them. Most ecosystems build the registry
first and inherit its problems permanently; gBASIC can ship the half that
matters and add discovery only if people ask for it.

Trust, testing and certification of published libraries are a separate document:
[library_trust_design.md](library_trust_design.md).

## What lives where (decided 2026-09-30)

This mechanism is mostly **not part of the language**, and the split follows the
rule this workspace already uses — separate when it CONSUMES the platform, which
is why Studio, whisker, the books and the site are all their own projects.

| | where | why |
|---|---|---|
| modifier scoping | **gbasic** | a language change |
| the capability registry, `--capabilities`, and its tripwire | **gbasic** | MEASURED: the dangerous surface is enumerated NOWHERE -- 360 scattered dispatch mentions in `eval.c`, `gi` alone 127 times. A scanner in another repo hardcoding today's eleven modules goes silently stale the day a twelfth lands, and every scan after that is incomplete. So the language REPORTS its surface and the scanner derives it -- the same reason run_docs_gate derives the builtin list from the reference instead of pinning it |
| runtime capability enforcement, if it happens | **gbasic** | same shape as `with principal` |
| manifest, fetch, submission pipeline, AI test generation, tiers, index, site | **separate project** | different cadence (a service against a versioned artifact), different dependencies (network, storage, model access against a deliberately lean C build), and the 157-suite gate must not grow either -- a gate that goes slow and flaky is one people turn off |

**And the separate project should be written in gBASIC**, which makes it the
strongest dogfood available: webserver, `http`, `process`, `persist`, the actor
pool and `tools`/`mcp` all exercised by something real, the role Studio plays for
the GUI and the site plays for the webserver.

**Not created yet, deliberately.** whisker taught us that a project folder made
before its scope is settled collects assumptions; the deterministic half is
specified first.

## 9. Open questions

1. **§4 identity** — name, or origin + version. Blocks everything else.
2. **§5 modifier scoping** — a language change, cheapest now.
3. Does a vendored library's own `libs/` get committed, or re-fetched? (Committed
   is the offline-forever answer; re-fetched is smaller.)
4. **Should a bare `load producer` find `libs/`?** Today it needs the explicit
   path of §7.5. Making the bare form work means letting `load` search `./libs/`
   — and it must be **ONE LEVEL ONLY**, because a recursive search is precisely
   what leaked the private copy in §7.3. Nicer to write, but it is a runtime
   change, against this document's own "the interpreter learns nothing" rule.
5. **Where does a library author's own relative `load` point?** A library
   written with `load c from "libs/c.bas"` only works vendored if it lands at
   `libs/<name>/<name>.bas`, which is why §7.3 nests. Worth pinning before
   anyone publishes, because it fixes the on-disk shape permanently.
6. What does a library declare its **gBASIC version** requirement against, given
   that this tree changes diagnostics between releases? See the compatibility
   axis in the trust document.
