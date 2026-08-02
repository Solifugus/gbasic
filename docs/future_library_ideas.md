# gBASIC — Future Library Directions

*A thinking document. Nothing here is committed scope — it's a map of the
territory, priorities as I'd argue them, and the open questions worth deciding
before any of it becomes real work.*

---

## Framing

Two forces should drive the library roadmap, and they sort the priorities on
their own:

1. **What gBASIC Studio will demand.** The IDE is now the first serious gBASIC
   program, and it will need capabilities within weeks. Those aren't
   suggestions — they're predictions, and they come first.
2. **The GI dividend.** The GObject-Introspection bridge changed the economics.
   Many "new libraries" are no longer C binding projects — they're thin gBASIC
   sugar over GObject libraries you never have to bind by hand. Before writing
   any new native module, the first question is now: *is there a GObject library
   for this?*

Everything below is organized around those two forces plus the distinctive bets
only this project is positioned to make.

---

## Tier 1 — Studio will force these

These are the near-certain near-term needs. If they don't exist, they'll be the
first entries Studio files in `DOGFOOD.md`.

### Subprocess / pipes
Spawn a child process, stream stdout/stderr, read exit codes, send signals/kill.
This is literally the Studio **run button**, and it's also how Studio's LSP
client talks to `gbasic-lsp`. If this doesn't already exist in some form, it's
the very first thing Studio needs.

### Regex
Find/replace in the editor; general text processing for any serious language.
PCRE2 as a native module is the obvious shape. *Agreed as a priority.*

### JSON-RPC framing
A small stdlib layer (Content-Length framing over a pipe) so Studio can speak
LSP to `gbasic-lsp`. Rides on top of subprocess/pipes. Small, but load-bearing
for the IDE's first real feature beyond "run."

---

## Tier 2 — The heritage layer

### Canvas / sound (the ECB heir)
`PSET`, `LINE`, `CIRCLE`, `DRAW`, `PLAY` as gBASIC-flavored commands over GTK4
drawing (and GStreamer for sound). This is the TRS-80 Extended Color BASIC
recreation reborn **as a library instead of a language** — Studio's immediate
window plus this library *is* the modern CoCo experience. Doubles as the Jupyter
kernel's inline-graphics story.

Note: this may be *subsumed by* the game library below (raylib covers 2D drawing,
sound, and input in one dependency). Worth deciding whether the "gentle heritage
canvas" and the "real game engine" are one library or two.

---

## Tier 3 — The GI dividend (sugar, not bindings)

State this as a **strategy**, not a list. The bridge means these are thin gBASIC
sugar layers, written in gBASIC itself, over libraries already reachable:

- **GStreamer** → audio: music, effects, mixing. (Also the `PLAY` backend.)
- **gdk-pixbuf** → image loading, scaling, format conversion.
- **Pango** → rich text rendering / measurement.
- **libsoup** → HTTP/websockets beyond the current webclient.
- **libmanette** → gamepads/controllers (GObject, full GI support).

The rule: don't write a C module for something a GObject library already does
well. Write the gBASIC sugar over the bridge.

---

## Tier 4 — The distinctive bets

Two things *nobody else is positioned to build*, because they fall out of
gBASIC's existing worldview.

### MCP library (server side especially)
Any gBASIC program exposing tools to AI agents in a dozen lines. Studio's agent
socket becomes a *library*. This is the WorkSplicer / Conatus worldview expressed
at the language level — gBASIC as a first-class citizen in agentic systems.

### GPIO / I²C / serial (the SBC bet)
"The BASIC for single-board computers" is an open niche with exactly the
hobbyist-immediacy appeal that made the CoCo matter. You own a Milk-V Mars.
`libgpiod` is small. Boot to a prompt, blink an LED in three lines — the original
joy, on open hardware.

---

## Tier 5 — Workaday fill-ins

As demand appears, not before:

- **CSV / tabular** (EDGAR and notebook workflows) — though see the spreadsheet
  engine, which dwarfs this.
- **Plotting** → PNG/SVG output; pairs with the Jupyter kernel.
- **zip / gzip** — and note this is the *gateway dependency* for spreadsheet
  files (xlsx/ods are ZIP archives of XML).
- **In-language testing library** — so *users* get the fixture-first culture the
  project itself runs on.

---

## Games & simulations — honest architecture

**GI helps at the edges but should not be the engine core.**

- **Wins for free via GI:** GStreamer (audio), libmanette (controllers),
  gdk-pixbuf (assets), and `GdkFrameClock` (a vsync'd tick *if* the game lives in
  a GTK window).
- **Where GTK runs out:** pushing sprites/tilemaps/particles at 60fps is not what
  GTK's scene graph is shaped for beyond casual/board/puzzle games. And the
  canonical game libs (SDL, raylib) are plain C with no GObject layer, so GI
  can't reach them.

**The exciting answer: bind raylib directly as a native module.** Raylib was
designed for education and simplicity — its API already reads almost like QB
graphics commands (`DrawCircle`, `DrawTexture`, `IsKeyDown`), covers 2D/3D/audio/
input in one dependency, and is plain C (fits the existing module conventions).

Use the Löve2D / PICO-8 model: engine hot paths in C, game *logic* in gBASIC via
`load` / `update(dt)` / `draw` callbacks. A tree-walking interpreter is
comfortably fast enough for the logic layer.

**The angle nobody else has:** gBASIC's actor model and watchers *are* game
architecture. Entities-as-actors-with-mailboxes is ECS-adjacent; `watch
player.health` firing UI updates is reactive game state for free. A game library
that leans into those instead of hiding them would be a genuinely distinctive way
to write games — not "BASIC with a game lib."

**Early discipline if pursued:** a frame-budget fixture (N sprites, assert 16ms)
in the suite from day one, so interpreter-throughput surprises show up as failing
tests rather than shipped stutter.

---

## The spreadsheet engine — the big one

This deserves its own document eventually, but the shape as it stands:

### The thesis
Not import/export — a **spreadsheet *engine*** that runs Excel/ODS-authored
workbooks **at scale**, with documents, worksheets, and formulas interoperating.
Internal representation is *not* zipped XML; files become views over a live
engine.

### Why it matters
Business people across every industry know spreadsheets and use them heavily —
but spreadsheets don't scale. The war story: at Health Plan Services, SharePoint
Excel Services was supposed to scale client-authored formulas (insurance payors
computing quotes) and *absolutely did not*, because it scaled by spinning up
Excel instances instead of maintaining one graph. The daily pain: enormous manual
effort keeping financial data in sync across separate workbooks.

### The correct mental model
A workbook is a **pure-functional dataflow DAG** that happens to have been
authored in a grid. (This is literally the "Build Systems à la Carte" framing —
cells are build targets, formulas are rules, recalc is an incremental rebuild.)
Excel's original sin — volatile functions and coarse invalidation forcing full
recalcs — is exactly what killed Excel Services.

### How it scales (computation)
- **Evaluate the DAG, never the grid** — a recalc touches only dirty cells'
  transitive dependents, in topological order.
- **Parallel evaluation** of independent subgraphs. (The actor model is already a
  work-distribution engine.)
- **Columnar ranges with incremental aggregates** — `SUM(A:A)` is a maintained
  running total, not a rescan.
- **Sparse storage** — real sheets are ~90% empty.
- **Formula compilation** — a formula filled down 100k rows is *one* compiled
  expression over a column vector, not 100k parsed cells. Often 10–100× vs.
  cell-at-a-time, and it's where "interpreted language" worries evaporate: you
  interpret per *distinct formula*, not per cell.

### How it becomes useful (the shape)
The inversion is the product: **the engine is the source of truth; workbooks
become views** that import from and export to it. "Keep these sheets in sync"
stops being manual labor and becomes what the engine *is* — cross-document
references are live subscriptions through the DAG. Three things fall out:

1. **A server, not a file** — many workbooks/users against one live engine. The
   thing Excel Services tried and failed to be.
2. **Formulas for the 95%, gBASIC for the 5%** — payors keep writing the formulas
   they know; the escape hatch for real logic is a real language, not VBA. Engine
   and gBASIC interoperate.
3. **Temporal by nature — the moat** — every cell carries its history (what
   changed, when, who, and what recomputed downstream). An *auditable* spreadsheet
   engine isn't a nicety for financial data; it's the compliance argument that
   gets it purchased. "Show me this quote's inputs as of the day it was sent" is a
   sentence Excel fundamentally cannot say.

One-line shape: **a temporal, server-hosted dataflow engine that runs the
workbooks people already wrote, treats files as views over a live graph, and is
programmable in a real language.** Not "Excel but faster" — "the source of truth
Excel pretends to be, at scale, with an audit trail."

### Priorities without access to real workbooks
No employer files needed — the **Enron corpus** (~15,000 real business
spreadsheets) is the public research dataset for exactly this. Function-frequency
studies on it converge: a Tier-1 vocabulary of ~25–40 functions (SUM, IF,
arithmetic, VLOOKUP, AVERAGE/COUNT/MIN/MAX, INDEX/MATCH, SUMIF/COUNTIF, basic
text/date) covers the overwhelming majority of real formulas. It also doubles as
the **test bed**, with **LibreOffice Calc headless as a differential oracle** —
evaluate the same sheet in both, diff the grids. Corpus-driven, fixture-first.

### Fixing Excel's mistakes (deliberately)
Give it a mechanism, not case-by-case courage: **correct semantics by default, a
quirks profile for opt-in bug-compat, and a `DELTAS.md`** listing every
intentional divergence with rationale (the D3 documentation pattern applied to
the engine).
- **The flagship divergence:** gBASIC has a **money type**. Currency columns in
  exact decimal by default — penny drift in financial models is a known,
  quietly-tolerated Excel disease. "Your workbooks, but the pennies add up" is a
  sentence a CFO understands.
- **Handle with care:** text/number coercion rules and empty-cell-as-zero are
  load-bearing in messy real sheets — diverge only behind the quirks flag.
  Dynamic-array spill vs. implicit intersection decides whether post-2020
  workbooks run at all — an explicit "which era of Excel" scoping decision, made
  early.

### Storage architecture (three separable layers)
Don't treat this as "pick a database." The layers scale differently:

1. **Hot recalc state → in-memory, always.** The resident dependency DAG with
   dirty-marking and incremental aggregates is pointer-chasing at memory speed.
   Round-tripping to SQL per cell would lose to Excel Services before starting.
   This is non-negotiable if "scale" means what we want.
2. **Durable authored state → SQLite (v1).** Single-file, transactional,
   embeddable; stores cells/formulas/structure (the durable input). You are *not*
   evaluating through it — it's a reliable disk format that isn't zipped XML.
3. **History / audit → AmorphDB (when ready).** Cell-value-over-time with full
   downstream-recompute lineage *is* a temporal tree-graph. This is where AmorphDB
   is the *right* model, not just an available one.

**Key discipline:** define persistence as an **interface** from day one
(`load_workbook`, `snapshot`, `record_change`) with a trivial file-based
implementation first. Then SQLite is one backend, AmorphDB another, and swapping
is an implementation detail — the same "seam before the second implementation"
rule that kept `libgbasic` clean. Build the in-memory engine now (the hard,
novel, valuable part; needs no database at all), prove the recalc thesis on the
Enron corpus, and let AmorphDB mature in parallel. The temporal layer is the
moat — and a moat goes around a keep that already stands, not into the foundation
poured first.

### The scope question to answer before falling in love
The ceiling is enormous but so is the surface, and this is a different *kind* of
project than the language work — closer to **a database with a spreadsheet's
face**. So the AmorphDB overlap may be foundational rather than incidental. The
real question, before it becomes its own multi-year world: is this a **gBASIC
library**, a **product built on gBASIC + AmorphDB**, or **the thing those two
projects were quietly building toward all along**? That decides its scope better
than any function-priority list.

---

## Suggested near-term ranking

If forced to order the *library* work against the actual trajectory:

1. **Subprocess / pipes** — Studio forces it (run button + LSP client).
2. **Regex** — Studio forces it (find/replace); useful everywhere.
3. **Canvas or raylib** — the deferred forty-year joy; decide if it's one
   library or two.

Everything else (MCP, SBC I/O, plotting, zip, the spreadsheet engine) is a
deliberate bet to schedule against appetite — with the spreadsheet engine being
less "a library" and more "possibly the most commercially interesting thing in
the portfolio," deserving its own design effort when its time comes.

---

## Open questions to sit with

- Is the heritage canvas and the game engine **one library or two**? (raylib may
  subsume the canvas.)
- Is the spreadsheet engine a **library, a product, or a convergence** of gBASIC
  and AmorphDB?
- Which **era of Excel** compatibility (pre- or post-dynamic-arrays) is the
  target?
- Does subprocess/pipes already exist in some form, or is it genuinely new work?
  (Determines whether Tier 1 has a hidden head start.)
