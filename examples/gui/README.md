# GUI Demo

This demo exercises the Stage 2 GTK proof of concept renderer only.

Current Stage 2 scope:

- static rendering from the initial record tree
- no live value synchronization back into rendered widgets
- no watcher integration with the GUI event loop
- no dynamic widget tree mutation after `gui.window(...)`

## Requirements

- GTK 3 runtime available
- `gbasic` built with GTK support
- `GBASIC_PATH` set so `load "gui"` can find [stdlib/gui.bas](/home/solifugus/development/gbasic/stdlib/gui.bas)

## Run

From the repository root:

```sh
GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas
```

## Manual Test Steps

1. Build with `make clean && make`.
2. Run `GBASIC_PATH=stdlib ./gbasic examples/gui/demo.bas`.
3. Verify a window titled `Demo` appears.
4. Verify the layout is vertical with spacing between rows.
5. Verify the first label shows `Hello gBASIC GUI`.
6. Verify the input starts empty.
7. Verify the button shows `Save`.
8. Verify the final label shows `Ready`.
9. Close the window and confirm the process exits cleanly.
