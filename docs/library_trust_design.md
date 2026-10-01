# Library trust: judging quality, compatibility and safety without a staff

**Status: Proposal, with one piece BUILT.** There is no submission pipeline, no
capability scanner and no certification.

**Built 2026-09-30: the capability registry and `gbasic --capabilities`** (§1.3),
because it is the one piece that must live in the language and it is unblocked by
every open question here and in the distribution document. `tests/run_capabilities.sh`
requires the table to cover the two lists the interpreter already maintains for
its own reasons, in both directions — so a module added without a classification
fails there rather than silently scanning as harmless. It found three
unclassified names the first time it ran. Companion to
[library_distribution_design.md](library_distribution_design.md), which covers
how a library is named, versioned and fetched.

Everything marked **measured** was run against the tree on 2026-09-30.

---

## 1. Why gBASIC can do this and npm cannot

Three properties, all measured, and together they are the whole argument:

### 1.1 Loading a library runs nothing — MEASURED

```basic
library evil
	print "I RAN AT LOAD TIME"     ' never printed
	...
```

Only the program body ran. **There is no install hook and no load-time
execution**, so npm's single largest trojan vector — `postinstall` — does not
exist. Fetching a gBASIC library is downloading a text file. Nothing an author
wrote can execute until the *program* calls it.

### 1.2 A string cannot become a callable — MEASURED

`f = "secret"` yields a **string**. There is no `eval`, no `require(variable)`,
no `getattr`. `spawn` will not even parse a computed name (`syntax error,
unexpected SPAWN`). Function *values* exist, but every one of them is named
literally somewhere in the source.

### 1.3 The dangerous surface is a closed, literal list — MEASURED

Eleven native modules — `process`, `webclient`, `http`, `webserver`, `smtp`,
`ldap`, `sqlite`, `pg`, `odbc`, `crypto`, `gi` — plus eleven file and directory
verbs (`exists`, `read`, `write`, `bytes`, `lines`, `chars`, `lock`, `unlock`,
`list`, `files`, `folders`). All dispatched by literal name. There is no FFI.

**Therefore what a library can do is exactly what its source textually says, and
a capability scan is a PROOF rather than an opinion.** That is not true of
JavaScript or Python and cannot be made true of them.

### 1.4 And the scan reads tokens, not text — MEASURED

A library whose comment says *"never calls process.run"* and whose string says
*"we do not use webclient.get here"* defeats `grep` (3 matches) and not
`--tokens`:

```
4:10   STRING            "we do not use webclient.get here"
7:10   QUALIFIED_IDENT   webclient.get          <- the only real use
```

The comment produces no token at all. **A capability scanner over `--tokens` is
perhaps fifty lines and cannot be fooled by comments, strings or
documentation** — which also makes it immune to prompt injection aimed at a
reviewer (§6).

---

## 2. Three axes, kept apart

A submission is judged on three separate things, and conflating them is how
"certified" comes to mean nothing.

| axis | asks | decided by |
|---|---|---|
| **Quality** | does it do what it claims, and fail properly when misused? | tests |
| **Compatibility** | which gBASIC versions, and which versions of each dependency? | the version matrix, §5 |
| **Safety** | does it do anything it does not claim? | the capability scan, §3 |

## 3. Decidable questions go to a machine. Only a machine.

| decidable → **deterministic**, every submission, free | not decidable → **AI**, §4 |
|---|---|
| the capability set, and its transitive closure | does behaviour match the prose? |
| the capability **diff** against the previous version | what inputs would break this? |
| content hash and immutability | are the refusals sensible, or does it refuse everything? |
| does it parse; do its own tests pass | is the documentation honest about edges? |

**The capability audit must never be an AI's job.** It is statically decidable
(§1), so handing it to a model replaces a proof with an opinion, and an opinion
that is wrong once has certified a trojan.

**The capability diff is the actual trojan defence.** Not "we reviewed it" —
which does not scale and does not survive version 1.0.1 — but *a library cannot
silently gain powers*. A four-line helper that acquires `net` in a patch release
is mechanically detectable, and `--fetch` refuses it without explicit consent.
Almost every npm attack that mattered was a benign package acquiring new
behaviour in a patch release.

## 4. AI generates evidence. It does not issue verdicts.

This is the load-bearing rule of the whole scheme.

Do **not** certify on a model's judgement. Have the model **write adversarial
tests**, then run them. The artifact is *the test suite and its recorded
results*, committed beside the library — never "the audit concluded".

Three things follow, and none of them is available from a verdict:

- **Reproducible.** Anyone can re-run it. A verdict cannot be re-run.
- **It survives a bad day.** A model that misses something on Tuesday has not
  permanently blessed the library; the tests remain, and a better model later
  *adds* tests rather than requiring anyone to re-trust an old judgement.
- **Non-determinism stops mattering.** Run the generator twice and get different
  tests — keep both. A non-deterministic *gate*, by contrast, is one people
  learn to disable.

It is the discipline this tree already runs on — goldens, controls, perturbation
— applied to somebody else's code.

**And the controls are not optional.** "Rejects bad input" is satisfied by a
library that rejects *everything*, so every refusal test must be accompanied by
its nearest LEGAL neighbour. A submission whose negative tests have no controls
is rejected mechanically, not as a judgement call. This tree learned that rule
the expensive way and it transfers directly.

## 5. The submission pipeline

1. **Parse and hash.** Immutable from here; the version is the content.
2. **Capability scan** (§3) — the set, and the diff against the previous
   version. A new capability requires explicit consent.
3. **Run the library's own tests**, recording the output.
4. **Does it do what it claims** — AI-written tests from the documentation, run
   and recorded (§4).
5. **Does it break properly** — AI-written misuse tests, each with its legal
   control.
6. **The version matrix.** Re-run 4 and 5 against **each declared version of
   each dependency**. gBASIC can do this because versions coexist
   (distribution §2.2), which npm cannot. The output is *recorded fact* rather
   than the author's claim: "verified against `c` 1.4.0 and 2.0.1; fails against
   2.1.0."
   **Bound it**: the pinned versions plus the current latest of each, never the
   cross product, or the pipeline becomes the bottleneck.
7. **The adversarial challenge** (§6), for the pipeline rather than the library.

## 6. The adversarial challenge, and why it is not optional

Once certification exists it is a **target**. Somebody will write a library
designed to pass.

So the pipeline gets the treatment every tripwire in this tree gets: **it must
be shown capable of failing.** One model writes a library that is malicious and
intended to pass; the pipeline tries to catch it. The valuable output is not
"we won" — it is that **every attack that succeeds becomes a permanent test
case**, exactly as a perturbation that goes green becomes a new tier here.

Without this, the pipeline is a check nobody has ever seen fail, which this tree
treats as indistinguishable from one that always passes.

**The specific attack to expect first** is prompt injection: a library whose
comments, README and identifiers are written to steer the model in steps 4 and
5. Two defences fall out of the design above — the capability scanner is immune
(§1.4: a comment produces no token), and **everything in a submission is data,
never instructions**, which is the rule this project already applies to anything
read from outside the tree.

## 7. Certification tiers, each claiming something checkable

No tier says *safe*. "Certified" reading as "safe" is the failure that makes
people less careful than they would have been with no scheme at all.

| tier | claims exactly | verified by |
|---|---|---|
| **0 Published** | content is immutable; these are its capabilities | machine, automatic |
| **1 Tested** | its own test suite passes here | machine, reproducible |
| **2 Contained** | its capability set is within a stated bound (e.g. no `process`, no `net`) | machine |
| **3 Reviewed** | a named person read **this exact version** | human — **expires at the next version** |

Tier 3 expiring is the important detail: a review that carries forward to the
next version is how certification becomes a lie.

**The core libraries start at the top for free.** The 65 stdlib libraries live
in this tree behind 157 suites; the repository publishes them with that
provenance attached rather than re-earning it.

## 8. What this honestly does not stop

Stated because a trust scheme that oversells itself is worse than none:

- A library with **legitimate** `net` access exfiltrating data. Capabilities
  bound the *kind* of harm available, not the intent.
- Wrong answers. Certification is not correctness.
- A dependency changing underneath — though transitive capability sets plus
  private vendoring make that change visible and pinned.
- A library that is fine today and whose *author* is compromised tomorrow. The
  capability diff is what makes the next version's change visible; nothing makes
  it impossible.

## 9. Build order

**The deterministic half first, with no AI at all**: capability scan, transitive
closure, capability diff, content hash, and a runner for the library's own
tests. That alone gives immutability, honest capability labels, and a refusal
when a patch release grows a new power — most of the real protection. It never
goes down, costs nothing per submission, and cannot be talked around.

AI-generated adversarial tests are increment two. They make the **quality**
claims real; they were never what made the **safety** claims real.

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

## 10. Open questions

1. **Where does the capability scan run** — at submission only, or also in
   `--fetch` against what is on disk? (The second makes tampering between
   publication and use detectable.)
2. **Should capabilities be ENFORCED at run time**, not merely reported? A
   program declaring that a library may not use `process`, with the interpreter
   refusing, is the same shape as `with principal`. It is the natural
   destination and a much larger change; the manifest is the first step toward
   it either way.
3. What is the **compatibility** claim against a gBASIC version, given this tree
   changes diagnostics between releases? A library pinned to "0.3.0 behaviour"
   needs that phrase to mean something — see the compatibility-policy question
   raised separately.
4. Who may award **Tier 3**, and what happens when the reviewer is the author?
