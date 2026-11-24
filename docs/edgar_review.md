# The EDGAR suite: where it stands

**Status:** Ruled 2026-11-18. Matthew: "one of the books planned will
actually make heavy use of EDGAR when we get to it. It will be proven then
for sure." So the live check (§4 item 1) and the verification of the 33
claims wait for that book, and run against the live SEC then. Until then,
the plan stays as it is. Matthew, 2026-11-18: "We should also take a review
of EDGAR when we get a chance."

**Live check run 2026-11-23** (Matthew: "go ahead and use my name and
email"), with the identity the July captures used. Every URL in
`examples/fixtures/edgar/MANIFEST.md` was fetched again, one a second (15
requests, all 200), into a scratch directory; the fixtures were not touched.
- The five XML filings are byte-identical to July's.
- Every JSON file has July's shape (key paths and value types compared),
  except that Apple's companyfacts gained a third taxonomy, `ffd` (filing-fee
  disclosure), beside `dei` and `us-gaap`. `fundamentals.bas` reads `us-gaap`
  then `dei` by name, so it is ignored.
- The three 10-K pages are July's bytes plus one `<script>` the SEC now adds
  before `</body>`. `mdna_sections_test` and `mdna_prepass_test`, fed the live
  pages, give their goldens byte for byte.
- `scorecard.bas` live runs clean; its figures differ from the golden only
  because Apple's fiscal 2026 10-K (year to 2026-09-26) was filed since July.
  `monitor.bas` (a copy with the identity set) completed its first poll and
  raised its alerts.

So the fixtures were not refreshed: nothing a library reads changed shape,
and refreshing would move goldens for new filings, not for a new format.

## 1. What was planned, and what was claimed

`docs/edgar_suite_development_plan.md` set out 33 work packages in seven
tracks over three designs:

- `edgar_design.md`: the SEC library and the analysis libraries;
- `xml_design.md`: the `xml` module;
- `llm_design.md`: `llm.bas`.

The plan's rule is that I claim and Matthew verifies. **All 33 were recorded
"done-claimed" in `docs/PROGRESS.md` between 2026-07-02 and 2026-07-05, and
none has a recorded verification.** The plan was never formally closed, and
`CLAUDE.md` still carries its session protocol ("One WP per session, assigned
by Matthew").

**What exists:**

- the libraries `edgar`, `fundamentals`, `forensics`, `insiders`,
  `ownership`, `mdna` and `screener` (about 3,100 lines), and `llm`;
- the `xml` module;
- the demo `examples/edgar/scorecard.bas`: a ticker in, a forensic dossier
  out (Piotroski, Beneish, Altman, Sloan, dilution, red flags).

## 2. What holds it today

- **27 offline goldens in `run_examples.sh`**, run on every gate, over SEC
  responses captured on 2026-07-02 (`examples/fixtures/edgar`, with a
  `MANIFEST.md`). They have stayed green through every language change since
  July: brace modifiers, three-valued unknown, UTC instants, sorting records,
  and errors in place of plausible answers.
- **The demo matches its golden today** (run offline, 2026-11-18).
- **`xml` and `llm`** have suites of their own (`run_xml_sign.sh`,
  `run_llm_transcript.sh` and others), extended well past the EDGAR plan by
  later work.

## 3. What nothing holds

- **The live SEC.** Online use needs `EDGAR_IDENT`, the contact identity the
  SEC requires in every request's User-Agent. No run against the live SEC is
  recorded since the July captures. **SEC response shapes can move in four
  months, and the goldens would not see it**, because they replay July's
  bytes. This review did not run one either: the identity would carry
  Matthew's contact details to sec.gov, and that is his to approve.
- **Whether the numbers are right.** The forensic scores were held to values
  computed by hand from the captured facts, and to published examples where
  there were any (`beneish_boeing_fixture.json`). They are self-generated
  evidence, as the finance libraries' are.
- **`examples/edgar/monitor.bas`**, the live filings monitor, is offline-tested
  through `monitor_harness_test` only.

## 4. Recommendations and questions for Matthew

1. **A live check, once.** Run the demo and the poll test against the live
   SEC with an identity Matthew chooses. Diff against the July captures, then
   refresh the fixtures from the live responses if their shape moved. It is
   about a dozen requests, inside the SEC's fair-access limit of 10 a second.
   **Q1: which identity string to send** (`Name email@example.com` is the
   SEC's form)?
2. **Close the plan.** Mark `edgar_suite_development_plan.md` as complete,
   and move `CLAUDE.md`'s "EDGAR suite work" section into that document,
   since there is no next work package. **Q2: close it, or do you want to
   verify the 33 claims first,** as the plan intended? Verifying would go
   track by track, using each PROGRESS entry's evidence block.
3. **Its future is the enterprise list's.** Anything new for EDGAR, such as
   XBRL frames or more forms, comes from a user's need. Nothing in the plan is
   left undone.
