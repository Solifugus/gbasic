# examples/scratch/

Deliberate playgrounds and exploration probes. **Nothing here is part of any
test suite** — no runner references these files and none has a golden `.out`/
`.err`. They are kept for hand-running and as behavior references (e.g. for the
`docs/ai/` documentation layer), not as regression tests.

Do not add golden files here or wire these into `tests/*.sh`. If a probe earns a
golden, promote it into `examples/` or `tests/` and add it to the relevant
runner's case list.

Current contents:

- `webclient_playground.bas` — interactive WebClient demo against the public
  `httpbin.org` service. Requires network; intentionally never run in CI.
- `exploratory_parser_condition_disambiguation.bas` — parser behavior probe
  (condition vs. call disambiguation).
- `exploratory_precision_equality.bas` — numeric/date precision equality probe.
- `exploratory_watcher_ordering.bas` — watcher fire-ordering probe.
