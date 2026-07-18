# ERRORS — gBASIC diagnostic & runtime-error catalog

> **Placeholder (Phase D1).** This file is filled in **Phase D3**: a catalog of
> diagnostic codes and runtime-error domains (code → meaning → typical cause →
> fix), generated from the `eval.c` error path where possible, with a note on how
> it was derived and how to regenerate it.

Seed material already collected:

- **`_scratch/D3_error_codes_harvest.md`** — the retired numeric `Error code:`
  values harvested from the stale negative fixtures (all `1003`) before they were
  rebaselined in Phase D0.5.

Explicit D3 task carried from the D0 audit (surprise **S13**): read the `eval.c`
error path and pin down the exact `on error resume next` **resume semantics** —
black-box tests showed local-resume in one case and whole-statement abandonment in
another, so the true model must come from source, not experiment. Once pinned,
correct the `gbasic_error_handling_gotcha` memory.
