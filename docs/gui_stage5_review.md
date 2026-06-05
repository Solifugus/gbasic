# Stage 5 Watcher Integration Review

Implementation status note:

- Stage 5A plumbing is implemented
- Stage 5B is implemented for queued GUI-originated watcher execution
- Stage 6A is implemented for refreshing existing GTK widgets from live record values after watcher execution
- GTK-originated input commits and button clicks enqueue internal GUI mutation records
- queued GUI mutations are batched and deduplicated within a flush cycle
- queued GUI mutations now trigger normal gBASIC watchers after the GTK event iteration
- existing rendered widgets now refresh from watcher-driven record changes
- dynamic widget tree mutation and structural rebuilds are still not implemented

## Overview

This document reviews how Stage 5 watcher integration should fit into the current gBASIC GUI implementation.

Scope of this review:

- no runtime behavior changes
- no GTK code changes
- no watcher integration implementation

The goal is to define a safe execution model for watcher-driven GUI behavior after Stage 4B.

## Current State

### Current watcher execution model

The current watcher runtime in [src/eval.c](/home/solifugus/development/gbasic/src/eval.c) has these properties:

- watchers are registered only at top level
- watcher declarations are currently name-based, for example `watch(a, b)`
- watcher registration runs the watcher immediately once
- assignment triggers watchers by root lvalue name only
- watcher execution is immediate, not deferred
- watcher execution is suppressed inside `without watchers`
- watcher execution is skipped outside the global environment

Relevant details:

- `watcher_trigger(name)` enqueues matching watchers and immediately calls `watcher_drain()`
- `watcher_drain()` runs synchronously and uses a simple reentrancy guard with `watcher_draining`
- there is no deduplication in the queue
- there is a hard stop after 10,000 queue steps

The important consequence is that current watchers are driven by language-level assignment statements, not by arbitrary in-memory record mutation.

### Current GUI mutation path

The current GTK integration mutates live widget records directly:

- `input` commit calls `gui_widget_set_string_field(widget_record, "value", ...)`
- `button` click calls `gui_widget_set_bool_field(widget_record, "value", 1)`

Those mutations affect the same live records reachable through `win.<id>`, which is correct for Stage 4B.

However, they currently bypass the normal watcher path because they do not go through `AST_STMT_ASSIGN`, `assign_lvalue(...)`, and `watcher_trigger(...)`.

### Current event-loop shape

`gui.run(win)` currently:

1. resolves the live window record
2. builds the GTK window from `win._root`
3. shows the window
4. loops on `gtk_main_iteration_do(TRUE)` until the window closes

That loop provides a natural safe point for deferred Stage 5 watcher execution.

## Main Architectural Constraint

There is a mismatch between the current watcher engine and the GUI design target.

Current watcher syntax and triggering model:

- syntax is `watch(a, b)`
- matching is by root variable name only
- `win.status.value = "Busy"` triggers `watcher_trigger("win")`

GUI design target:

- examples use path-oriented watches such as `watch win.save.value`
- backend-originated changes should conceptually trigger watchers for the changed widget field

This means Stage 5 cannot safely be treated as only "call `watcher_trigger(...)` from GTK". Some watcher runtime changes are required first.

## Recommended Architecture

### Recommendation

Do not run watchers directly inside GTK signal handlers.

Use a deferred GUI mutation queue and flush it after the current GTK event finishes, inside the existing `gui.run(win)` event loop.

Recommended model:

1. GTK signal handler mutates the live widget record.
2. GTK signal handler records a pending GUI-originated mutation.
3. GTK signal handler returns immediately.
4. `gtk_main_iteration_do(TRUE)` returns to the gBASIC event loop.
5. The event loop flushes pending GUI-originated mutations.
6. Watchers run from normal gBASIC runtime code, not from GTK callback frames.
7. Future record-to-GTK synchronization runs after watchers settle.

This keeps GTK as an event source, not the place where language execution is re-entered.

### Why not run watchers immediately in GTK signal handlers

Immediate watcher execution inside GTK signal handlers is risky:

- GTK reentrancy becomes much harder to reason about
- watcher code may update records that later drive GTK updates
- future record-to-GTK synchronization could re-enter GTK while GTK is already mid-signal
- error handling becomes ambiguous if watcher code raises a runtime error from a GTK callback
- nested watcher execution from GTK callbacks is harder to debug than execution from the main interpreter loop

The current watcher engine is synchronous and recursive enough already. Embedding that directly inside GTK signal callbacks would amplify the risks.

### Recommended batching granularity

Recommended batching level:

- batch per GTK event-loop cycle

Practical meaning:

- signal handlers may record multiple field mutations
- after `gtk_main_iteration_do(TRUE)` returns, flush the batch once
- deduplicate repeated writes to the same watched target within that batch

This is preferable to per-change immediate execution because it:

- reduces duplicate watcher runs
- handles Enter plus focus-loss style duplicate commits more cleanly
- gives a clear phase boundary between GTK event processing and gBASIC watcher execution

It is also preferable to large cross-event batching because GUI behavior should still feel prompt and deterministic.

## Recommended Stage 5 Execution Model

### For input commit

Recommended sequence:

1. GTK `activate` or `focus-out-event` fires.
2. Backend commits the current entry text into the live widget record.
3. Backend records a pending mutation for that widget field.
4. GTK signal handler returns.
5. The `gui.run(win)` loop regains control after the GTK event iteration.
6. Pending GUI mutations are coalesced.
7. Matching watchers run.
8. Future Stage 6 record-to-GTK synchronization applies any watcher-driven UI changes.

### For button click

Recommended sequence:

1. GTK button click fires.
2. Backend sets the live widget record `value = true`.
3. Backend records a pending mutation for that widget field.
4. GTK signal handler returns.
5. The `gui.run(win)` loop regains control.
6. Pending GUI mutations are coalesced.
7. Matching watchers run.
8. Future Stage 6 record-to-GTK synchronization applies any watcher-driven changes, including resetting the button if user code sets `value = false`.

## Proposed Runtime Phases

The safest long-term model is a phased loop:

1. GTK event phase
2. gBASIC watcher phase
3. backend sync phase

Suggested semantics:

- GTK event phase:
  collect backend-originated mutations only
- gBASIC watcher phase:
  run watchers until the mutation queue is drained or a safety limit is hit
- backend sync phase:
  apply record-to-GTK updates caused by watcher execution

This makes source-of-truth transitions explicit.

## Sequence Diagrams

### Input commit

```text
User
  -> GTK entry signal
  -> backend signal handler
  -> mutate live widget record
  -> enqueue GUI mutation
  -> return to GTK
  -> gtk_main_iteration_do(TRUE) returns
  -> gui.run loop flushes GUI mutation batch
  -> watcher engine runs
  -> future Stage 6 sync applies record->GTK updates
```

### Button click

```text
User
  -> GTK button signal
  -> backend signal handler
  -> set widget.value = true
  -> enqueue GUI mutation
  -> return to GTK
  -> gtk_main_iteration_do(TRUE) returns
  -> gui.run loop flushes GUI mutation batch
  -> watcher engine runs
  -> future Stage 6 sync applies record->GTK updates
```

### Unsafe immediate model to avoid

```text
GTK signal handler
  -> mutate record
  -> run watchers immediately
  -> watcher mutates more records
  -> future GTK sync tries to update widgets
  -> nested GTK activity / reentrancy risk
```

## Feedback Loop and Storm Risks

### Recursive feedback loops

The main future loop risk is:

1. GTK input mutates record
2. watcher mutates record
3. backend sync mutates GTK widget
4. GTK emits another signal
5. loop repeats

To prevent this, Stage 5 and Stage 6 need explicit origin tracking.

Recommended mechanism:

- mark backend-driven GTK refresh operations as sync-originated
- suppress GUI-originated mutation enqueueing while backend sync is applying GTK updates

Equivalent implementation shapes could use:

- a `gui_sync_depth` counter
- a `gui_backend_applying_updates` flag
- per-window suppression state

### Watcher storms

Current watcher queues do not deduplicate repeated watcher entries. That is acceptable for simple scripts but is weak for GUI event traffic.

Recommended Stage 5 behavior:

- deduplicate pending GUI mutation targets within a single event-loop cycle
- then trigger watchers once per unique target set

If Stage 5 continues using only root-name watcher triggering, deduplicate by root name at minimum.

### GTK reentrancy

GTK reentrancy risk is the strongest reason to avoid immediate watcher execution in signal handlers.

The rule should be:

- GTK signal handlers mutate records and enqueue work
- only the main `gui.run(win)` loop executes gBASIC watcher logic

### Stale UI state

Before Stage 6, stale UI state will remain possible after watcher execution because watcher-driven record mutations still do not refresh GTK live.

That is acceptable if documented, but Stage 5 should be designed so Stage 6 can slot in immediately after watcher draining.

## Interaction With Future Record-to-GTK Synchronization

Future record-to-GTK synchronization should run after watcher execution, not before.

Recommended order:

1. GTK-originated mutation enters record tree
2. watchers run against record truth
3. record tree settles
4. backend sync applies final record state to GTK

This avoids transient GTK updates for intermediate watcher states.

Example:

1. button click sets `win.save.value = true`
2. watcher sets `win.status.value = "Saving..."`
3. watcher performs work
4. watcher sets `win.status.value = "Saved."`
5. watcher sets `win.save.value = false`
6. backend sync applies the settled final values

Without that ordering, the backend may repaint multiple intermediate states unnecessarily.

## Recommended Trigger Granularity

### Best near-term option

For the current runtime, the safest near-term option is:

- batch per event-loop cycle
- trigger watchers once per affected root window variable

That fits the current watcher engine more naturally than path-granular triggering.

### Best long-term option

For the GUI design as documented, the better long-term option is:

- path-aware watcher dependencies
- batch per event-loop cycle
- deduplicate by exact changed field path

That would align with examples such as `watch win.save.value`.

## Required Runtime Changes Before Safe Stage 5

The following changes appear necessary before Stage 5 can be implemented safely.

### 1. Add a deferred GUI mutation queue

Needed because current GTK handlers mutate records immediately but have no safe bridge into watcher execution.

The queue should store at least:

- owning window
- changed widget record or changed widget id
- changed field name
- watch-trigger identity

### 2. Store watcher-trigger identity for GUI windows

Current GUI bindings know the widget record but not the root watcher identity that should be triggered later.

At minimum, Stage 5 needs the root lvalue name for the window passed to `gui.run(win)`.

Without that, a GTK-originated mutation cannot trigger current watchers consistently.

This is especially important because `gui.run(...)` currently accepts non-lvalue expressions as a fallback. Those values can render, but they do not provide a stable root symbol for watcher triggering.

### 3. Decide how GUI watches map to the current watcher language

This is the largest semantic gap.

Options:

- short-term:
  trigger `watch(win)` style watchers only
- longer-term:
  extend watcher syntax and matching to support field paths such as `win.save.value`

The current GUI design examples assume the longer-term model, but the current parser and watcher runtime do not support it.

### 4. Add deduplication for GUI-originated watcher triggers

Without deduplication, input commits and chained mutations can enqueue redundant watcher runs quickly.

### 5. Add explicit sync-origin tracking

This is required before Stage 6, and Stage 5 should be designed with it in mind now.

Otherwise, future record-to-GTK refresh can cause endless loops or duplicate watcher triggers.

### 6. Define GUI-loop error handling

If watcher execution during `gui.run(win)` raises a runtime error, the runtime needs a clear policy:

- stop the GUI loop and surface the error
- route through existing `on error` behavior
- or isolate GUI event errors differently

The current non-GUI runtime already has error machinery, but GUI-loop integration needs an explicit design choice.

## Risks

### Semantic mismatch risk

The current watcher engine is root-name based, while the GUI design is field-path oriented.

If Stage 5 is implemented without addressing that mismatch, GUI behavior may work only through broad `watch(win)` handlers and not through the more readable widget-field watches shown in the design examples.

### Reentrancy risk

Immediate watcher execution inside GTK callbacks risks difficult nested control flow and eventual GTK misuse once live sync exists.

### Error propagation risk

Watcher failures during GUI event handling need a deterministic policy. Silent failures or partially applied event batches would be hard to debug.

### Performance risk

A large window with many watcher-driven assignments can create queue churn quickly if batching and deduplication are weak.

### State convergence risk

If Stage 5 runs watchers but Stage 6 live GTK sync is not present yet, the record tree may change correctly while the visible UI stays stale. That gap is acceptable temporarily, but it should be documented clearly.

## Recommended Implementation Plan

### Step 1

Document the semantic boundary:

- Stage 5 will use deferred watcher execution
- not direct execution in GTK signal handlers

### Step 2

Add per-window GUI mutation queue state and root trigger identity.

### Step 3

Change GTK signal handlers so they:

- mutate the live record
- enqueue mutation metadata
- do not run watchers directly

### Step 4

Add a post-iteration flush step inside the `gui.run(win)` loop:

- drain pending GUI mutations
- trigger watchers using the chosen granularity

### Step 5

Add deduplication and safety limits for GUI-originated trigger batches.

### Step 6

Define GUI-loop runtime error behavior.

### Step 7

After Stage 5 is stable, implement Stage 6 record-to-GTK synchronization using explicit sync-origin suppression.

## Recommendation Summary

Recommended Stage 5 architecture:

- do not run watchers in GTK signal handlers
- queue GTK-originated mutations
- flush them after each GTK event-loop iteration
- trigger watchers from normal interpreter code
- prepare for future record-to-GTK synchronization as a separate post-watcher phase

Most important prerequisite:

- resolve the mismatch between current root-name watcher triggering and the GUI design's path-oriented watcher expectations

Most important safety rule:

- keep GTK callback code narrow and side-effect-limited; let the `gui.run(win)` loop own watcher execution
