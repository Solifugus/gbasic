# MD&A Panel — worked example transcript

**Status: worked example, NOT a test.** The automated coverage for the panel is
`examples/mdna_panel_test.bas` (shape/schema over fixture models). This file is
the human-facing companion the WP-MDA-3 plan asks for: it shows what a *live* run
looks like end-to-end so a reader can see the shape of the fight. The verdict and
referee JSON below are representative of a real run against local models
(Ollama/vLLM) with a frontier referee — reproduce it yourself by pointing the
handles at live endpoints (see "How to reproduce"). It was assembled by hand in an
offline environment, so treat the specific numbers as illustrative, not as a
recorded model call.

Subject: **Crocs, Inc. (CIK 1334036)**, FY2024 → FY2025 10-Ks
(`examples/fixtures/edgar/tenk_crox_2024_sample.htm`, `..._2025_sample.htm`).

## The program

```basic
program main(args)
    load mdna from "../stdlib/mdna.bas"
    load llm  from "../stdlib/llm.bas"

    ' Volume seats: free local models. Referee: a paid frontier model. (llm_design §5)
    bull_m     = llm.local("http://localhost:11434/v1", "llama3.3:70b")
    bear_m     = llm.local("http://localhost:11434/v1", "qwen2.5:72b")
    forensic_m = llm.local("http://gpubox:8000/v1",     "mixtral:8x22b")
    frontier   = llm.anthropic("claude-sonnet-4-6", unknown)   ' key from ANTHROPIC_API_KEY

    ' Deterministic pre-pass first — the fight happens over arithmetic.
    prior = mdna.sections(read_html("...tenk_crox_2024_sample.htm"))
    curr  = mdna.sections(read_html("...tenk_crox_2025_sample.htm"))
    scorecard = forensics.flags(...)                 ' supplied by forensics.bas
    ev = mdna.evidence(prior, curr, scorecard)

    panelists = [
        { name: "bull",     model: bull_m,     stance: mdna.stance_bull() },
        { name: "bear",     model: bear_m,     stance: mdna.stance_bear() },
        { name: "forensic", model: forensic_m, stance: mdna.stance_forensic() }
    ]

    verdicts = mdna.panel(panelists, curr, ev)
    print("candor disagreement: " + string(mdna.disagreement(verdicts)))
    final = mdna.referee(frontier, verdicts)
    print(final.verdict)
end program
```

## Deterministic evidence handed to every analyst (from WP-MDA-2)

```
risk_added_count   = 137
risk_removed_count = 119
hedge_shift        = +21 per 10k words (MD&A hedging 139 -> 160)
scorecard          = { m_score: -1.78, accrual_ratio: 0.08, red_flags: [ "rising DSO", "negative FCF" ] }
```

## Panel verdicts (one row per analyst, decoded via `llm.ask_json`)

**bull** (`stance_bull`):
```json
{ "candor": 78, "stance_read": "Management is direct about tariff exposure and margin normalization; brand momentum is real.",
  "evasions": [], "citations": ["risk_removed_count", "MD&A: 'gross margin normalized'"] }
```

**bear** (`stance_bear`):
```json
{ "candor": 34, "stance_read": "The rise in hedging language and 137 new risk sentences undercut the confident tone; free cash flow is barely discussed.",
  "evasions": ["no direct FCF walk", "tariff quantification deferred to risk factors"],
  "citations": ["hedge_shift", "scorecard.red_flags"] }
```

**forensic** (`stance_forensic`):
```json
{ "candor": 51, "stance_read": "Narrative and numbers mostly agree, but the hedging shift and negative-FCF flag are not addressed head-on.",
  "evasions": ["hedging density rose 15% YoY without acknowledgement"],
  "citations": ["hedge_shift", "scorecard.m_score"] }
```

`mdna.disagreement(verdicts)` → **variance 358** on the candor column (78 / 34 / 51)
— a split panel: the uncontroversial reading would converge, this one does not.

## Referee (frontier model, sees only the verdicts + evidence)

```json
{ "verdict": "lean negative",
  "candor": 44,
  "rationale": "Two of three analysts flag the undiscussed hedging shift and negative FCF; the bull case rests on brand momentum that the filing supports but does not reconcile with the forensic scorecard. The M-Score is below the manipulation threshold, so this is a candor concern, not a fraud signal.",
  "dissent": "Candor spread 34-78; the bull weighted narrative tone, the bear and forensic weighted the arithmetic." }
```

## How to reproduce

1. Serve two local models (e.g. `ollama serve`, and a vLLM box) and export
   `ANTHROPIC_API_KEY` for the referee.
2. Point the handles above at your endpoints.
3. Run the program. Because local models are the sloppy ones, `llm.ask_json`'s one
   corrective retry earns its keep here; a panelist that still won't return JSON
   becomes an `ok=false` row and the panel continues (see the `muddled` seat in
   `examples/mdna_panel_test.bas`).
