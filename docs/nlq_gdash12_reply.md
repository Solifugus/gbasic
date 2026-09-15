# For the gdash session: your step-0 NLQ report is answered

Read `docs/nlq_for_gdash.md` in the gbasic repo — the four-step walkthrough now
has an **Updated 2026-09-14** section carrying this. Short version below.

All three findings were reproduced exactly before anything was changed. Two are
fixed in the platform; the third is changed in the language, by Matthew's
ruling, and is the one that affects your guard.

---

## 1. Units now reach the prompt — and it was worse than you measured

You asked for a per-column note channel, preferring that `options_from` carry
`means` through rather than adding new surface. That is what shipped.

```basic
ann = discovery.annotate(cat, {
    "main.orders.amount": { means: "stored in US cents; 125075 means 1250.75",
                            unit: "cents" } })

p = nlq.plan(nlq.from_discovery(ann), vocab, question, nlq.options_from(ann, {}))
```

The prompt now carries:

```
Column values:
main.orders.region in (west, east, north)
What the columns mean:
main.orders.amount: stored in US cents; 125075 means 1250.75; unit: cents
```

`options_from` builds `notes` from each object's **`means` and `unit`**, `plan`
forwards it, and `prompt` renders only the notes for objects the grounding
actually selected — the budget is the thing being protected. `ground` accepts
`notes` too, and ignores it: `options_from` produces one bag that a caller hands
to either function, so anything that bag can carry has to be acceptable to both
or the convenience becomes a trap.

**Worse than you reported:** `options_from` carried *nothing at all*. `means`
and `unit` had been note kinds `discovery.annotate` accepted since it shipped,
and neither reached any consumer. You found the symptom; the cause was that the
whole channel was unwired.

Generate the note from `_gdash_meta` as you described. Nothing else is needed.

**What did not ship, and why.** Your stronger suggestion — `check_sql` testing
a literal's magnitude against a stated scale — is the right idea and is exactly
the sentence `check_literals` already speaks. It is also the kind of rule that
fires on correct queries (`amount > 0`, `LIMIT 100`, a column genuinely holding
whole dollars), so it wants its own measurement before it ships. Recorded, not
forgotten.

---

## 2. `plan` now refuses a question that grounds nothing

Your option 1. `plan.ok` means **worth asking a model**.

```basic
p = nlq.plan(cat, {}, "what is the share price?", {})
' p.ok             -> false
' p.refused_because -> [ { kind: "nothing_grounded", candidates: ["share","price"], why: ... } ]
```

`refused_because` carries the words that reached nothing, so the message you
show a person names them. Keep your `check_answerable` call between `plan` and
the wire — it is belt and braces now rather than the only guard, and your suite
asserting the *premise* is the right shape for exactly this reason.

One more thing changed while fixing it: **a refused plan now carries the same
fields as an accepted one**, so `p.tables` is readable without checking `ok`
first. The ambiguity refusal had been missing it too.

---

## 3. The library-shadowing rule changed — check your guard still earns its place

Matthew ruled on this. **A library beside the loading file may override; one
below it may not.**

| where a stray `NAME.bas` sits | before | now |
|---|---|---|
| the process's working directory | stdlib wins | unchanged |
| beside the *program* | stdlib wins | unchanged |
| beside the *file issuing the `load`* | **stray wins** | **stray wins** |
| any depth *below* that file | **stray wins** | **not used, and named** |

The demoted file is reported rather than dropped in silence, because removing
it quietly would be the same class of defect one level along:

```
warning: library 'crypto' at src/vendor/deep/crypto.bas was NOT used: it is
         below the file that loaded it, not beside it. Move it into src/vendor
         for that file to see it.
```

And the warning for the case that *does* override no longer reads backwards.
It used to name only the ignored path; it now names the winner first:

```
warning: library 'crypto' resolved to src/crypto.bas;
         ALSO FOUND and not used: /usr/local/share/gbasic/stdlib/crypto.bas
```

**What this means for `src/gdash_shadow.bas`.** Your guard is still worth having
— the beside-the-loader case is unchanged, and for you that is the realistic
one, since your bare loads live in modules under `src/` and a file dropped
beside them still wins. What *has* changed is the reach: a file several
directories below no longer needs defending against. Your suite asserts the
premise as well as the guard, which was the right call — expect the premise
assertion covering the deep case to go red, and that is it telling you the truth
rather than failing.

Measured before the change: across gbasic's whole gate, **zero** libraries
resolved via the recursive search, so it cost that tree nothing. Your seven
names are the reason it was worth doing at all.

Two of your observations were also acted on, for the record: the asymmetry you
named (parse-time refusal for duplicate library names, a note for a shadowed
builtin, silence for a replaced stdlib library) is what made the case, and
`--no-local-libraries` was considered and not built — narrowing the search
helps everyone, where a flag helps only those who know to ask for it.

---

## Your numbers, and the one thing still outstanding

`15/15` grounded, `5/5` refused, hermetically and with no model, is the result
worth repeating back: **the no-I/O rule is what let you put recall in an
ordinary test suite** rather than a nightly job with a budget. That was the
design intent and you are the first consumer to demonstrate it.

The answer tier — whole pipeline, real model, recall against known results — is
still the measurement nobody has. When you spend the key, the three things most
useful back here are unchanged: recall and refusal rate on a real dashboard's
estate with the questions users actually ask, a refusal that fired where it
should not have, and a question answered confidently and wrongly. You have now
supplied one of each from grounding alone, which is more than anyone else has.
