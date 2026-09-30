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

## 7. The mechanism

- **`libs/`** holds vendored `.bas` files, committed to the project's own repo.
- **A manifest** (`gbasic.deps`) lists, per dependency: name, exact version,
  origin, content hash.
- **`gbasic --fetch`** reads the manifest, walks the graph, vendors each
  library's dependencies *privately* (§3), verifies every hash, and writes
  `libs/`. Run once; commit the result.
- **The interpreter learns nothing.** `load` finds a file exactly as it does
  today. **Running a program touches no network and needs no tool**, which is
  the property that makes dependencies "always work": at run time there is no
  dependency machinery left to fail.

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

## 9. Open questions

1. **§4 identity** — name, or origin + version. Blocks everything else.
2. **§5 modifier scoping** — a language change, cheapest now.
3. Does a vendored library's own `libs/` get committed, or re-fetched? (Committed
   is the offline-forever answer; re-fetched is smaller.)
4. What does a library declare its **gBASIC version** requirement against, given
   that this tree changes diagnostics between releases? See the compatibility
   axis in the trust document.
