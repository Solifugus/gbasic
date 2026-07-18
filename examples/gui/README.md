# GUI Demo

These demos exercise the current GTK proof of concept through Stage 6A.

These are **manual display tests**: they need a GTK 3 runtime and a live display,
so they are not in the golden suite. An automated **parse-only** smoke,
`tests/run_gui_parse.sh` (PLAN.md Phase D0.6, B6), runs `gbasic --ast` over every
file here — parse, don't run — to catch silent syntax rot without a display.
Actually launching them is manual; see [Manual Test Steps](#manual-test-steps).

Current implemented scope:

- static rendering from the current record tree at `gui.run(win)` time
- `gui.window(...)` returns a stable window record
- widgets are addressable as `win.<id>`
- pre-run record mutations are reflected in the first rendered window
- committed input changes update the live widget record
- button clicks set the live button `value` to `true`
- queued GUI-originated value changes trigger normal gBASIC watchers after the GTK event iteration
- watcher-driven record changes now refresh existing GTK widgets after watcher execution

Still not implemented:

- dynamic widget tree mutation after `gui.window(...)`

Current Stage 6A note:

- refresh is limited to already-rendered widgets
- no structural rebuilds or dynamic widget creation/removal happen yet

## Requirements

- GTK 3 runtime available
- `gbasic` built with GTK support
- `GBASIC_PATH` set so `load "gui"` can find [stdlib/gui.bas](/home/solifugus/development/gbasic/stdlib/gui.bas)

## Run

From the repository root:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas
```

Access-pattern demo:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/access_demo.bas
```

Watcher demo:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/watch_demo.bas
```

Calculator demo:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/calculator.bas
```

Unit converter demo:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/unit_converter.bas
```

## Manual Test Steps

1. Build with `make clean && make`.
2. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas`.
3. Verify a window titled `Demo` appears.
4. Verify the layout is vertical and the container uses the declared `spacing` distribution.
5. Verify the first label shows `Hello gBASIC GUI`.
6. Verify the input starts empty.
7. Verify the button shows `Commit`, not `Save`.
8. Verify the final label shows `Changed before run`.
9. Type text into the input, then press Enter or move focus away.
10. Click the button.
11. Close the window and confirm the process exits cleanly.

Access demo follow-up:

1. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/access_demo.bas`.
2. Verify the first printed line before the window opens is `Ada`.
3. In the window, change the input text and commit it with Enter or focus loss.
4. Click the `Commit` button.
5. Close the window.
6. Verify the next printed line is the committed input text.
7. Verify the final printed line is `true`.

Watcher demo follow-up:

1. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/watch_demo.bas`.
2. Change the input text and commit it with Enter or focus loss.
3. Verify `name committed` and the committed text print in the terminal.
4. Verify the visible status label updates to `Committed: ...`.
5. Click the `Save` button.
6. Verify `save clicked` prints in the terminal.
7. Verify the status label updates to `Saved via watcher`.
8. Verify the button caption returns to `Save` and the button is no longer visually down.
9. Close the window.
10. Verify the final printed line is the last watcher-written `win.status.value`.

Calculator demo follow-up:

1. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/calculator.bas`.
2. Change either input and commit it with Enter or focus loss.
3. Click `+`, `-`, `*`, or `/`.
4. Verify the result label updates immediately.
5. Verify the status label reports the operation.
6. Try a non-numeric input and verify the status label reports which side is invalid.
7. Try dividing by `0` and verify the status label reports the error.

Unit converter demo follow-up:

1. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/unit_converter.bas`.
2. Change the Celsius or Fahrenheit input and commit it with Enter or focus loss.
3. Click `C -> F` or `F -> C`.
4. Verify the other input updates immediately.
5. Verify the result label and status label update.
6. Click `C -> K` and verify the result label shows Kelvin.
