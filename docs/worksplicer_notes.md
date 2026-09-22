# WorkSplicer → gBASIC notes

Append-only. The WorkSplicer session records friction, gaps and asks here; this
session triages them. One entry per point: what was being built, what was
awkward or impossible, and a concrete suggestion.

**The rule that makes this work:** WorkSplicer does not fix gBASIC. It records.
A platform change made from inside an application is a change made to suit one
caller, and this tree has the machinery — gates, perturbation proofs, roster
tripwires — precisely so that platform changes are made once and made properly.

The same loop already runs for [gdash](gbasic_dogfood_notes.md) and
[tedderland](tedderland_dogfood_notes.md), and it has produced real work: the
`ldap` module, `web.configure`, `req.form`, the `chart` categorical-x defect,
`money.currency`, PLAT-NUL's whole sweep. **Three of those were reported as one
thing and measured as something worse**, which is the argument for recording
rather than guessing.

---

## What WorkSplicer is likely to need, predicted 2026-09-21

Written before the application exists, so that a prediction can be compared with
what actually happens. Recorded as *predictions*, not as commitments — several
of these will be wrong, and which ones is the useful information.

- **Presence** — who is in a workspace, what they are looking at. Probably an
  application concern over `pg` plus SSE, but if several applications want it,
  it is a library.
- **A control/lease primitive** — grant, share, revoke, expire, pre-emptive.
  `presence-and-control.md` §4 requires revocation to be *server-side and
  pre-emptive*, which is easy to implement wrongly in a way that passes testing
  and fails under latency. If that turns out to be subtle, it is platform work.
- **Optimistic concurrency helpers** — a write carrying the version it was based
  on, refused *with what changed*. `pg` has what is needed; the pattern may be
  worth a library.
- **Semantic description of a rendered view** (`ws.describe`) — the vocabulary a
  model reasons over. Almost certainly application-shaped, but if it is not, it
  belongs near `tools`.
- **Long-lived SSE under a worker pool** — `run_web_stream` covers parking and
  poking a stream. A workspace open all day, across a rolling reload, is a
  longer-lived case than anything measured.
- **Per-asset resource limits** — an asset type runs in a spawned worker;
  timeouts and memory ceilings are possible through `process`, and what the
  numbers should be is unmeasured.

**Prediction about the predictions:** the real asks will mostly not be on this
list. They rarely are.

---

## Entries

*(none yet — the application has not been built)*
