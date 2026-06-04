# gBASIC Declarative GUI Design

## 1. Overview

This document defines a future declarative GUI system for gBASIC.

Implementation status note:

- current implementation is Stage 2 only
- Stage 2 is limited to static GTK rendering of the initial record tree
- Stage 2 does not include live value synchronization
- Stage 2 does not include watcher integration with the GUI event loop
- Stage 2 does not include dynamic widget tree mutation after `gui.window(...)`

The core model is:

```text
GUI state is a live record tree
```

Widgets are represented as records. Container widgets hold child widgets in a `contains` array. User interaction mutates widget records, and watchers observe those mutations to implement behavior.

This keeps GUI programming aligned with existing gBASIC features:

- records represent structured state
- arrays represent ordered children
- nested lvalue assignment updates deep widget fields
- dynamic record key access supports generic traversal and lookup
- watchers react to changes without callback registration
- encode/decode makes GUI state serializable where useful

The intended source style is declarative and data-first:

```basic
load "gui"

ui = {
    id:"main",
    component:"vert",
    contains:[
        { id:"name", component:"input", value:"" },
        { id:"save", component:"button", label:"Save", value:false },
        { id:"status", component:"label", value:"Ready" }
    ]
}

win = gui.window(400, 300, "Demo", ui)

watch win.save.value
    if win.save.value then
        win.status.value = "Saving..."
        save_project()
        win.status.value = "Saved."
        win.save.value = false
    end if
end watch

gui.run(win)
```

The GUI layer should treat widget state as ordinary gBASIC data, not as opaque toolkit handles.

## 2. Relationship to GTK

GTK is the intended first backend for Unix/Linux, but GTK should not be exposed as the public API.

User code should describe:

- widget identity
- component type
- values
- label where applicable
- containment
- simple layout hints

User code should not describe:

- GTK widget classes
- GTK signals
- GTK object ownership
- GTK-specific layout containers
- GTK naming or lifecycle rules

The public abstraction is the record tree and a small `gui` library surface. GTK is an implementation detail used to render and synchronize that tree.

This separation matters for long-term portability. If the public API stays centered on records, values, and watchers, the same GUI description can later be rendered by other backends without changing application logic.

## 3. Widget Model

Each widget is a record. The common fields for v1 are:

- `id`
- `component`
- `value`
- `label`
- `contains`
- `enabled`
- `visible`
- `width`
- `height`
- `spacing`

Proposed meanings:

- `id`
  - Required string identifier for lookup and watcher-friendly access.
- `component`
  - String naming the widget kind, such as `"vert"` or `"button"`.
- `value`
  - Primary mutable state for the widget.
- `label`
  - Optional visible caption for components that use separate labeling, such as buttons.
- `contains`
  - Optional array of child widget records for container components.
- `enabled`
  - Optional boolean. When `false`, the widget is visible but not interactive.
- `visible`
  - Optional boolean. When `false`, the widget is hidden.
- `width`
  - Optional width hint.
- `height`
  - Optional height hint.
- `spacing`
  - Optional spacing hint for container children.

Reasonable v1 defaults:

- `enabled = true`
- `visible = true`
- `contains = []` for containers
- `spacing = 0` when omitted
- `button.value = false` when not explicitly set

Id rules:

- every widget must have an `id`
- duplicate ids within a window are illegal
- `gui.window(...)` should fail with a clear error if duplicate ids exist

Not every field applies to every component. The record model permits a uniform shape while allowing component-specific interpretation.

## 4. Component Semantics

The first component set is intentionally small.

### `vert`

Vertical container.

- lays out children top to bottom
- children come from `contains`
- `spacing` controls vertical gap between children
- `value` is normally unused

### `horiz`

Horizontal container.

- lays out children left to right
- children come from `contains`
- `spacing` controls horizontal gap between children
- `value` is normally unused

### `label`

Read-only text display.

- displays `value`
- no direct user editing

### `input`

Single-line text input.

- displays and edits string `value`
- typing does not update `value` on every keystroke
- `value` updates only when input is committed
- commit occurs on Enter or loss of focus

### `button`

Pressable control with state.

- visible caption comes from `label`
- pressed/down state comes from `value`
- clicking mutates `value` to `true`
- the program resets `value` to `false` when the action is complete

### `spacer`

Layout filler or fixed empty area.

- occupies space without user interaction
- `value` is normally unused
- `width` and `height` may define requested size

## 5. Value Semantics

The `value` field is the main state channel between the GUI backend and user code.

GUI behavior should primarily use `value`. v1 should avoid separate binding declarations.

Component-specific v1 meaning:

- `label.value`
  - Displayed text.
- `input.value`
  - Current input text.
- `button.value`
  - Current pressed/down state.
- `spacer.value`
  - Unused placeholder in normal operation.
- `vert.value`
  - Unused in normal operation.
- `horiz.value`
  - Unused in normal operation.

Notes:

- `label.value` should be the preferred way to change visible label content from code.
- `button.label` names the button, while `button.value` tracks whether it is currently pressed or still considered active.
- `input.value` represents the committed text value, not intermediate typing.
- Unused `value` fields still exist because uniform widget records are simpler than multiple incompatible shapes.

## 6. Watcher-Based Behavior

v1 should use watchers instead of callbacks.

The intended model is:

1. The backend mutates widget record values when the user interacts.
2. Normal gBASIC watcher machinery observes those value changes.
3. Watcher code updates other widget fields or performs application work.
4. Those record mutations flow back to the backend, which updates the rendered UI.

For inputs, watcher-visible changes occur on committed values only. Intermediate typing is backend-local until Enter or focus loss commits the new `value`.

This keeps GUI behavior in ordinary language code:

- no signal registration
- no callback function signatures
- no toolkit event objects in user code

Example:

```basic
watch win.name.value
    win.status.value = "Name changed"
end watch
```

Example with button-driven work:

```basic
watch win.save.value
    if win.save.value then
        win.status.value = "Saving..."
        save_project()
        win.status.value = "Saved."
        win.save.value = false
    end if
end watch
```

This model fits gBASIC especially well because watchers already exist and GUI state is ordinary data.

## 7. Button Behavior

Buttons in v1 are stateful inputs, not fire-and-forget events.

Defined behavior:

1. The button starts with `value = false`.
2. A click sets `value = true`.
3. The button remains visually down while `value = true`.
4. User code performs the relevant work.
5. User code resets `value = false`.
6. The button returns to its normal visual state.

This is intentionally not a traditional momentary button callback. The persistent `true` value provides visible feedback that the action is still in progress or has not yet been acknowledged by program logic.

Benefits:

- no separate click event API is required
- long-running work can keep the button visibly active
- program logic remains explicit
- watchers can coordinate button state with labels and other controls

This pattern should be documented clearly because it differs from many GUI toolkits.

## 8. Window Creation

The proposed construction API is:

```basic
win = gui.window(width, height, title, ui)
```

Parameters:

- `width`
  - Initial window width hint.
- `height`
  - Initial window height hint.
- `title`
  - Window title.
- `ui`
  - Root widget record for the live UI tree.

The returned `win` should provide:

- access to the root widget tree
- practical access to widgets by `id` where possible
- the handle passed into `gui.run(win)`

Desired usage:

```basic
win.save.value = true
win.status.value = "Busy"
```

This implies the window object should expose named widget references for reachable ids where practical. Since every widget has an id and duplicate ids are illegal within a window, the backend can maintain a reliable id-to-widget mapping for watcher-friendly access.

An implementation may choose for `win` to contain:

- window metadata
- the root UI tree
- resolved references to id-addressable widgets
- backend-private state not directly exposed to user code

The public-facing model should still feel like ordinary record access.

## 9. Event Loop

The proposed execution API is:

```basic
gui.run(win)
```

`gui.run(win)` should:

- create or realize the backend window
- render the widget tree
- enter the GUI event loop
- propagate user input into widget `value` fields
- propagate programmatic widget changes back into the rendered UI

After `gui.window(...)`, the widget tree structure is fixed for v1. `gui.run(win)` should therefore operate on a stable tree whose values and properties remain live and mutable.

For v1, a single top-level blocking event loop is sufficient. The first implementation should prefer simplicity over advanced lifecycle features such as multiple loop modes, nested modal stacks, or asynchronous scheduling APIs.

## 10. Backend/Rendering Responsibilities

The backend, initially GTK, is responsible for synchronizing two worlds:

- the gBASIC record tree
- the concrete rendered widgets

GTK backend responsibilities should include:

### Render the record tree

- walk the root widget record
- instantiate the appropriate backend widget for each `component`
- build container hierarchies from `contains`
- apply visible captions, displayed values, size hints, enabled state, and visibility

### Update widget values from user input

- when the user commits an input, update `input.value`
- when the user clicks a button, set `button.value = true`
- avoid exposing raw toolkit events to user code

### Update rendered widgets from gBASIC record values

- when `label.value` changes, update the visible label text
- when `input.value` changes from code, update the text field
- when `button.value` changes, update pressed/down appearance
- when `visible` or `enabled` changes, update the backend widget state

### Maintain id-to-widget mapping

- map widget ids to record references or backend bindings
- reject duplicate ids during window construction with a clear error
- support efficient lookup for synchronization
- support the `win.save.value` style where implemented

### Avoid feedback loops

- distinguish backend-originated updates from programmatic updates where necessary
- prevent redundant watcher churn or infinite synchronization cycles

The backend should be a translator and synchronizer, not the source of application behavior.

## 11. Future Portability

The record-tree design is intended to be backend-neutral.

Possible future renderers:

- GTK
- web
- TUI

Portability follows from keeping the public model abstract:

- `component:"vert"` means vertical layout, not a specific GTK box class
- `value` means widget state, not a backend event payload
- layout remains limited in v1 to `vert`, `horiz`, `spacing`, `width`, and `height`
- watchers express behavior in gBASIC, not in toolkit callback systems

Examples of later reinterpretation:

- GTK renderer creates native desktop widgets
- web renderer maps components to DOM elements and browser events
- TUI renderer maps components to terminal regions and keyboard-driven focus/input

Some components may eventually need backend-specific limitations, but v1 should avoid baking backend assumptions into the public data model.

## 12. Open Design Questions

The following questions remain unresolved and should stay open until implementation planning becomes more concrete.

### Watcher integration inside the GUI loop

- When exactly should watchers run after backend-originated value changes?
- Should they run immediately, batched, or at explicit safe points in the event loop?
- How should watcher-triggered UI updates be flushed back to the backend?

### Nested id exposure

- How should nested widgets be exposed on `win`?
- Is `win.save` sufficient as the practical access pattern for unique ids?
- Is a path-based lookup helper needed later for ids that cannot become direct fields?

### Validation and error reporting

- How should malformed widget trees be reported?
- Should unknown components fail at window construction time?
- Should missing required fields produce runtime errors with source location where possible?

### Styling model

- Should style live in the same widget record?
- Should there be a separate theme or style record?
- How much style should v1 omit in order to preserve portability?

### Layout rules

- Should layout rely mainly on `width` and `height` hints?
- Can v1 stay minimal with only `vert`, `horiz`, `spacing`, `width`, and `height` without painting itself into a corner?

### Mutation semantics

- Structural GUI mutation is deferred until a later version.
- After `gui.window(...)`, the widget tree structure is fixed.
- Widget values and properties remain live and mutable.

### Window model scope

- Is one window enough for the first implementation?
- Should dialogs later be ordinary widget trees or separate window objects?

## 13. Minimal Implementation Plan

The first implementation should be staged conservatively.

### Stage 1: Design document only

- write and review this design
- clarify open questions
- avoid code or dependency changes

### Stage 2: GTK proof of concept rendering static tree

- load a UI record tree
- render `vert`, `horiz`, `label`, `input`, `button`, and `spacer`
- enforce required ids and reject duplicate ids
- no full live synchronization required yet

### Stage 3: Live value updates

- reflect programmatic changes from gBASIC records into rendered widgets
- establish internal id mapping

### Stage 4: Button and input value mutation

- propagate committed input changes into `input.value`
- propagate button clicks into `button.value = true`
- render button captions from `label`
- show button pressed/down state from `value`

### Stage 5: Watcher integration

- ensure backend-originated value changes trigger normal watchers
- ensure watcher-originated UI updates refresh rendered widgets cleanly

### Stage 6: BAG GUI experiment

- build a small experiment using the BAG project or a BAG-adjacent sample
- validate that the model is expressive enough for a nontrivial interface
- use the experiment to refine missing layout and state features before broadening the component set

## Summary

The proposed GUI system is a small declarative layer centered on live records, uniform widget fields, value-driven interaction, and watcher-based behavior. GTK is the first renderer, not the public abstraction. If that boundary is preserved, the same GUI description can later target desktop, web, or terminal backends with minimal change to application code.
