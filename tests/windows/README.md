# The native Windows suite

`smoke.bas` is a **self-checking gBASIC file**. Run it on either platform:

```
gbasic tests/windows/smoke.bas
```

`mismatches: 0` is a pass. Any other last line is a failure, and each one names
what it got and what it wanted.

## Why it is written in gBASIC rather than PowerShell

The gate is 158 bash suites and Windows has no bash. Porting them to PowerShell
would be a bigger project than the port itself, and it would leave **two gates
that can disagree** about what they assert — the failure this tree has tripwires
against in four other places.

The tree already uses the answer everywhere: dozens of suites are self-checking
`.bas` files that state their own expected values and print `ok` or a `MISMATCH`
naming both sides, with the bash runner doing nothing but invoking the binary.
On Windows, **the `.bas` file is the suite.** It needs no shell, and it is the
same file on both platforms, so the two platforms cannot disagree.

## Why a native run is not optional

`windows_port_status.md` §5: **a socket is not a file descriptor on Windows**, so
`read`/`write`/`close` on one compiles cleanly and fails at **runtime**. The
cross-compile ratchet can never see that class. Something has to run on Windows,
and this is the cheapest honest something.

## What it covers, and what it deliberately does not

It asserts values (arithmetic, floored `mod`, codepoints against bytes, interior
NULs, money, dates, durations), that the interpreter found its own stdlib after
being moved (which is `gb_exe_path`, one of the three platform mechanisms), files
and paths, **all three line-ending conventions on the way in**, that a path with
an interior NUL is refused rather than truncated (the 0.4.0 security fix, whose
Windows failure mode would be a file appearing under the wrong name), and
`process.run`, which is the one place the port needs a genuinely different
mechanism and must still answer the same shape.

It does **not** cover actors (the channel refuses on Windows by design), the GUI
or the webserver — all out of Tier 1 per `windows_port_plan.md` §5.

## The bug this file shipped with for ten minutes, kept as a warning

The first draft counted with two scalars and a helper function. gBASIC has no
closures, so `checks = checks + 1` inside the helper created a function-local and
the outer counter never moved: **every line printed `ok`, the summary said
`checks: 0`, and a failing check would not have been counted either.** A suite
that reports success regardless of its results is worse than no suite.

The language said so, at warning **2104**, naming the remedy — and it was missed
by reading only stdout. The tally is a record now, and the **self-check at the
bottom fails if fewer than twenty checks were counted**, because that is the one
defect this file cannot otherwise report about itself. Proven red both ways:
reverting the tally to scalars prints `BROKEN: only 0 checks were counted`, and a
deliberately wrong expectation is reported and counted.
