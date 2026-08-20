# gBASIC charting library — design proposal

Status: Phases 1 AND 2 IMPLEMENTED 2026-08-20 (stdlib/chart.bas -- line, scatter, bar, histogram; tests/run_chart.sh); design reviewed and amended the same day
(text-layout policy §6b, refusal policies in §8, structural verification in §9,
open questions resolved in §13)

A pure-gBASIC library, `stdlib/chart.bas`, that turns a frame (or bare lists)
into a chart. It is the visualization layer the statistics and EDGAR toolkits
are missing: both already emit tables of numbers and stop there. This library
renders those numbers as **SVG text** — a string you can drop into the
WebServer site, embed in an HTML page, save to a file, or (later) hand to the
GTK module.

## 0. Motivation (why now, and why this shape)

Everything upstream of a picture already exists. `frame.bas` cleans and reshapes
data; `stats.bas` computes regressions, distributions, and diagnostics; the
EDGAR suite produces fundamentals trends and forensic scorecards. None of it can
be *seen*. A chart library is the single highest-leverage addition because it is
a multiplier on work already done rather than a new silo — and it dogfoods
`frame.bas` (a chart takes a frame), the string surface, and `unknown`
(missing cells become gaps).

The design goal is: **`chart.line(df, "date", "revenue")` returns a complete SVG
string, and that string is byte-for-byte deterministic** so it can be
golden-file tested like everything else in the tree.

## 1. Decision A — output format: SVG (not PNG, not ASCII)

A chart is a string of SVG markup. This is the crux decision and everything else
follows from it.

- **SVG is text**, so generating it needs no image library, no C code, and no
  new optional dependency. It is ordinary string construction — exactly what a
  pure-gBASIC library should be.
- **SVG renders where gBASIC already reaches.** The WebServer can serve it
  inline; an EDGAR dossier can embed it; a `.svg` file opens in any browser.
- **SVG output is deterministic**, so a chart is golden-file testable. Given the
  same frame and options, `render()` must produce the identical string every
  run — no timestamps, no floating-point locale drift, no map-ordering
  nondeterminism. This is the property that lets Adrian validate charts the same
  way he validates everything else: a `.bas` + a sibling `.out`.

Rejected alternatives:

- **PNG / raster** — needs a C imaging dependency (libpng/cairo) and a font
  rasterizer; it would have to be a C module and would not be text-diffable.
  Crosses the "earn a C module" bar for no benefit the site doesn't already get
  from SVG.
- **ASCII/Unicode terminal charts** — a real want, but that is the *TUI*
  rendering concern and belongs with the planned unified-UI work, not here.
  Sparklines (§14 Phase 4) are the one text-mode primitive worth folding in
  because dossiers want them inline in tables.

## 2. Decision B — pure gBASIC, not a C module (earn-it case, crossed)

The XML and crypto modules earned C because they wrap real native machinery
(libxml2, libcrypto) that would be reckless to reimplement. Charting wraps
nothing: it is arithmetic (data-space → pixel-space) plus string assembly. There
is no native library to bind, no performance cliff a C rewrite would fix at
realistic chart sizes, and no security surface. It stays in `stdlib/` and is
built entirely from the existing value model.

## 3. Decision C — input model: frames first, lists as the escape hatch

A chart consumes the same frame shape `frame.bas` produces (a record of
equal-length columns; `unknown` = missing). The primary calls name columns:

```
svg = chart.line(df, "date", "revenue")
svg = chart.scatter(df, "assets", "revenue")
svg = chart.bar(df, "quarter", "eps")
svg = chart.histogram(df, "daily_return")
```

For data that isn't already a frame, every chart type also accepts bare lists
through a `*_xy` form, so callers aren't forced to build a frame first:

```
svg = chart.line_xy(dates, revenues)
svg = chart.scatter_xy(xs, ys)
```

Multi-series is expressed by passing a **list of y-column names**; each becomes a
series with its own color and a legend entry:

```
svg = chart.line(df, "date", ["revenue", "net_income", "fcf"])
```

`unknown` cells break a line (gap) rather than plotting as zero, and are skipped
in scatter/histogram — consistent with how list stats already treat NA.

## 4. Decision D — API shape: convenience one-liners over a spec record

Two layers, same engine underneath.

**Layer 1 — one-liners** (the 90% path): `chart.line`, `chart.bar`,
`chart.scatter`, `chart.histogram`, each returning a finished SVG string with
sensible defaults.

**Layer 2 — spec + render** (the composable path): a chart is a plain record, so
it can be built up and inspected before rendering.

```
spec = chart.spec("line", df)
spec = chart.x(spec, "date")
spec = chart.y(spec, ["revenue", "net_income"])
spec = chart.title(spec, "Apple — top line vs. bottom line")
spec = chart.size(spec, 720, 360)
svg  = chart.render(spec)
```

The one-liners are thin wrappers that build a spec and call `render`. Options are
a plain options record with documented defaults (§7), following the
copy-on-write, new-record-per-transform idiom of `frame.bas` — no hidden state,
nothing to dispose.

## 5. Chart types (v1 scope)

| type        | x axis        | y axis    | notes                                           |
|-------------|---------------|-----------|-------------------------------------------------|
| `line`      | numeric/date  | numeric   | multi-series; `unknown` = gap; optional markers |
| `scatter`   | numeric       | numeric   | multi-series by color; optional fit line (§12)  |
| `bar`       | category      | numeric   | grouped and stacked variants                    |
| `histogram` | numeric (bin) | count     | auto or fixed bin count; NA skipped             |

`area`, `pie`, and `heatmap` are deferred to Phase 4 — they are styling
variations on the same coordinate machinery, not new machinery.

## 6. Scales, ranges, and "nice" ticks

The heart of the library is mapping data space to a pixel box inset by margins
that leave room for axes and labels.

- **Linear numeric scale** for continuous axes: `pixel = m0 + (v - dmin) *
  (m1 - m0) / (dmax - dmin)`.
- **Category scale** for bar x-axes: evenly spaced bands, one per distinct
  category, with configurable inner/outer padding.
- **Date scale**: since the datetime redesign, `d2 - d1` is a signed exact
  duration, so a date reduces to a day count with core arithmetic —
  `(d - anchor).total_seconds / 86400` — then plots on the linear scale; tick
  labels format back to dates. (An earlier draft referenced forensics.bas's
  private `_civil_days` trick; the language has since made it unnecessary.)

Axis bounds default to the data range, extended to **"nice" round numbers** via a
loose-labeling pass (the Wilkinson/Talbot style: pick a tick step from
{1, 2, 2.5, 5} × 10^k that yields a small, human-readable set of ticks covering
the range). Bounds are overridable through options for fixed-scale comparisons
across dossiers. Tick generation is fully deterministic — no floating-point
formatting that varies by locale; numbers are rendered through a single
in-library formatter with fixed precision rules.

## 6b. Text layout: exact alignment, estimated reservation (2026-08-20)

The library cannot measure text — SVG carries no font metrics, and the font
that finally renders belongs to the viewer. The policy is two-layered, and the
layers have different truth standards:

- **Alignment is EXACT, because the renderer does it.** Every label is placed
  with `text-anchor` (`end` to right-align y-axis labels against the axis,
  `middle` to center titles and x-ticks) so the library never needs to know
  where text ends — only where it is anchored. No estimate is involved in
  where text visually sits.
- **Space reservation is an ESTIMATE, with declared knobs.** Margins must be
  sized before rendering (the left margin has to fit `1,500,000`), and for
  that `estimated_width = len(label) * char_ratio * font_size` is sufficient:
  over-reserving by 5% is invisible, and the knobs are in the options record —
  `font_size` (default 12, used in both the SVG and the estimates) and
  `char_ratio` (default 0.6). Auto-derived margins come from the LONGEST
  formatted tick label; each margin is individually overridable
  (`margin_left:` etc., `unknown` = derive) for when someone's font or taste
  disagrees. This is stated as an approximation, not hidden as magic numbers.
- **Density is bounded by DETERMINISTIC TICK THINNING**, never by collision: a
  max-labels budget per axis, met by keeping every k-th tick. The thinning is a
  pure function of the tick list and the budget, so it cannot flap between
  runs.
- **Belt-and-braces**: SVG's `textLength` attribute can force a label into its
  reserved box (the renderer compresses glyph spacing). Offered as an option,
  off by default — squished text is its own ugliness, but it guarantees
  containment for pathological labels.

## 7. Options / styling (defaults chosen for the site + print)

An options record, merged over documented defaults:

| option            | default                      | meaning                              |
|-------------------|------------------------------|--------------------------------------|
| `width`,`height`  | 640 × 360                    | SVG viewport in px                   |
| `title`           | `unknown` (omitted)          | centered heading                     |
| `x_label`,`y_label`| column name                 | axis titles                          |
| `palette`         | fixed N-color ordered list   | series colors (see §13)              |
| `grid`            | `true`                       | light horizontal gridlines           |
| `legend`          | auto (on when >1 series)     | series key                           |
| `x_min`/`x_max`   | auto (nice)                  | fixed bounds override                |
| `y_min`/`y_max`   | auto (nice)                  | fixed bounds override                |
| `markers`         | `false` (line)               | draw point dots                      |
| `bins`            | auto (Sturges/√n)            | histogram bin count                  |
| `stacked`         | `false` (bar)                | stacked vs. grouped                  |
| `font_size`       | 12                           | pt; used in the SVG and the estimates|
| `char_ratio`      | 0.6                          | estimated glyph width / font_size    |
| `margin_left` etc.| `unknown` (derive)           | explicit number = you take over      |
| `max_ticks`       | per-axis budget              | deterministic thinning bound (§6b)   |

Text is rendered as `<text>` with a generic `font-family: sans-serif`; the
library never embeds a font (keeps output small and deterministic; the renderer
supplies the glyphs). No inline JavaScript, no external references — a single
self-contained `<svg>` element that carries a `viewBox` alongside
`width`/`height`, so an embedded chart scales responsively for free.

**Escaping is a correctness AND a security property.** Every string of user
origin — titles, axis labels, category names, legend entries — passes through
one `_escape()` (`&`, `<`, `>`, `"`, `'`) at the point it enters the markup.
This is what makes "no inline JavaScript" true rather than aspirational when
the SVG is concatenated into a served page, and it is pinned by a golden whose
title is hostile (`R&D <script> "q"`).

## 8. Missing data, refusals, and edge cases

The guiding rule is the house one: **a plausible wrong picture is worse than an
error.** Where the input is ambiguous, the library refuses by name rather than
guessing (2026-08-20).

- **`unknown` in a line series** → the path is broken at that x (a genuine gap,
  not a drop to zero), matching how a reader expects a missing quarter to look.
- **`unknown` in scatter/histogram** → the point/observation is skipped.
- **`nan`/`inf`** → treated exactly like `unknown` (gap / skipped). Statistics
  can legitimately produce them, and plotting an infinity is not a picture.
  Detection is `x != x` for nan (IEEE) plus a finite-range check.
- **All-NA or empty series** → renders axes and title with an explicit "no data"
  note rather than raising, so a dossier with a sparse concept still lays out.
- **Single data point / zero range** → the nice-tick pass pads the range so a
  flat series still draws on a sane axis instead of dividing by zero.
- **Non-numeric y** → raises with a clear message (charts plot numbers; category
  data belongs on the x-axis of a bar chart). **`money` is accepted** and
  plotted at face value — frames from the xlsx pipeline carry it, and forcing a
  conversion dance would be ceremony; `datetime`/`duration` y-columns are
  refused.
- **Duplicate categories in a bar chart** (two rows both `"Q1"`) → REFUSED by
  name: "category 'Q1' appears twice; aggregate before charting." Aggregation
  belongs to the frame tooling; a chart inventing a sum is a wrong picture.
- **Stacked bars with negative values** → REFUSED in v1, with the message
  naming diverging stacks as the future answer. A naive stack draws
  overlapping nonsense that looks fine at a glance.
- **More series than the palette has colors** → REFUSED, stating the series
  count and palette size; passing a longer custom `palette:` lifts the cap.
  Silent color cycling puts two series in the same jersey.
- **Unsorted x in a line chart** → plotted in ROW ORDER, documented: your
  order is your statement. The library never sorts behind the caller's back.
- **Unequal column lengths in a frame** → refused by column name.

## 9. Determinism and testing (the golden-file win)

Because output is a deterministic string, charts test exactly like the rest of
the tree: a `.bas` that builds a known frame and prints `chart.render(...)`,
plus a sibling `.out` holding the expected SVG. Requirements that make this hold:

- no timestamps, random IDs, or `Math.random`-style content in output;
- element and attribute emission order is fixed by code, not by map iteration;
- all numbers formatted through one in-library formatter (fixed decimal rules,
  no locale, no trailing-zero drift);
- color assignment is by series index against the ordered palette.

A handful of golden cases (one per chart type, plus a multi-series and an
all-NA case) pins the rendering. This is a large part of why SVG was chosen over
raster: Adrian can validate a chart without ever looking at a picture.

**A golden defends whatever it records** — this tree has learned that twice —
so goldens are not the only tier (2026-08-20). Every rendered chart in the
suite is also fed back through **our own `xml.parse`** for a structural pass:

- the output is well-formed XML (which catches escaping bugs generically, on
  inputs no golden thought of — the hostile-title case is the pinned example);
- every plotted coordinate falls inside the declared `viewBox`;
- the element census matches expectations (one `<path>` per line series, one
  `<rect>` per bar, a `<text>` per surviving tick).

The tier SKIPs cleanly when the build has no libxml2; the goldens still run
everywhere. This is the same shape of check the xlsx work called the
independent reader (§13.U there): not proof, but a second pair of eyes that is
not our own renderer agreeing with itself.

## 10. Performance notes

SVG assembly is string building. One O(n²) trap still matters here: repeated
string **concatenation** copies the whole accumulator each call. (`append` and
`arr[i]` are LINEAR since the array copy-on-write work — an earlier draft of
this note said otherwise, and PLAT-ARRIDX is what pins the current truth.) At
realistic chart sizes (hundreds of points) even the string trap is irrelevant,
but the implementation should still:

- iterate rows with `for each`, never `while i < count()` with `arr[i]`
  indexing;
- accumulate SVG fragments into a parts list and `join` once at the end rather
  than concatenating in a loop, and build a series' path `d` attribute the same
  way.

These are the standard idioms already used elsewhere in the stdlib; called out
here only because a renderer is concatenation-heavy by nature.

## 11. Non-goals (v1)

- interactivity, tooltips, zoom, animation (SVG can host them later via a
  separate opt-in; the core stays static and JS-free);
- raster/PNG export (would require a C imaging module — deliberately declined);
- terminal/ASCII rendering (belongs to the unified-UI/TUI track, except inline
  sparklines);
- 3D and geographic projections;
- statistical computation — the library *plots* a fit line but does not compute
  it; the caller passes fitted values (keeps `chart` and `stats` decoupled).

## 12. Integration examples

**EDGAR dossier — fundamentals trend** (drops straight into the scorecard demo):

```
load chart from "../../stdlib/chart.bas"
' trend is a FY-ascending frame with columns end, revenue, net_income, fcf
svg = chart.line(trend, "end", ["revenue", "net_income", "fcf"])
write_text(p, svg)     ' p(file)= "aapl_topline.svg"
```

**Statistics — scatter with regression fit** (fit computed by `stats`, plotted
by `chart`):

```
model = stats.ols(df, "y", ["x"])
df2   = frame.with_column(df, "yhat", predict_fn)   ' fitted values as a column
spec  = chart.new("scatter", df2)
spec  = chart.y(spec, ["y", "yhat"])                ' raw points + fit series
svg   = chart.render(chart.x(spec, "x"))
```

**Site — inline in a served page**: the WebServer handler concatenates the SVG
string into its HTML body; no file, no asset pipeline.

## 13. Open questions — RESOLVED with Matthew, 2026-08-20

1. **Default palette** — Okabe–Ito: fixed, ordered, colorblind-safe, 8 colors,
   good on screen and in print. A house palette can layer on later through the
   `palette:` option.
2. **`render()` output** — the bare `<svg>…</svg>` fragment is the primitive
   (embeddable anywhere); `chart.page(spec)` wraps it in a minimal standalone
   HTML document as a thin convenience.
3. **Number formatting** — one sane fixed default now (fixed decimal rules,
   thousands separators on axis labels); per-axis format options later.
4. **Dark mode / theming** — deferred; light-only v1, until the unified-UI
   theming story exists.
5. **Scope of v1** — the roadmap's own phasing: Phase 1 is line + scatter only
   (which alone unblocks the EDGAR trend and stats fit-plot cases); bar +
   histogram are Phase 2.

One implementation note, settled at build time: `chart.new(...)` PARSES at
the call site (qualified library calls do not hit the keyword-after-dot trap
that record fields do) — but a library CANNOT DEFINE a function named `new`
(`unexpected NEW` at the declaration), so the constructor is `chart.spec`.

## 14. Roadmap (phases)

Each phase is independently shippable and testable with golden files.

### Phase 1 — engine + line & scatter
Coordinate mapping, linear + date scales, nice-tick labeling, margins/axes/grid,
the in-library number formatter, the spec record and `render`, and `line` /
`scatter` (single and multi-series) with `line_xy` / `scatter_xy`. Golden tests
per type. This phase alone unblocks the EDGAR fundamentals trend and the stats
scatter-plus-fit use cases.

### Phase 2 — bar & histogram
Category scale (grouped and stacked bars), histogram binning (auto + fixed),
count axis. Golden tests. Unblocks forensic scorecard bars and stats
distributions.

### Phase 3 — polish
Legends, titles/subtitles, axis labels, gridline styling, marker options,
fixed-bound overrides for cross-dossier comparison, the colorblind-safe palette,
and `chart.page()` HTML wrapper. Golden tests for the composite output.

### Phase 4 — extended types
`area`, `pie`, `heatmap` (correlation matrices for stats), and inline
**sparklines** (a tiny axis-less line for use inside dossier tables). Each is a
styling variation on the Phase 1–2 machinery.
