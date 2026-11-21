# The profiler: a design note

**Status:** Increments 1 (exact counts) and 2 (sampled time, waiting, the
signal paths) **shipped** 2026-11-21: `gbasic --profile`,
`tests/run_profile.sh`. Increment 3 is design. Where the build differs from
what this note first said, the note now says what was built (§3-§7).
Matthew, 2026-11-20: "A gbasic profiler is a good thing. We should do that
one." The six questions in §9 were ruled 2026-11-21: "agreed on all six", each
as recommended.

## 1. What it answers

**"Where does my program spend its time?"** It answers per function and per
line of source, in the program and in every library it loads.

```
$ gbasic --profile orders.bas
...the program's own output, unchanged...
profile: 2.41 s, 24100 samples, 12.4% of them waiting
functions               calls     total     self
  parse_row             40000     61.2%    48.0%
  post_invoice            812     30.5%     4.1%
  accounting.post         812     26.4%    26.4%
  main                      1    100.0%     3.1%
lines                    runs      self
  orders.bas:17         40000     31.0%   amount = {USD}row.cells[4]
  orders.bas:19         40000     17.0%   rows += [record]
  accounting.bas:212      812     12.3%   ledger.entries += [e]
```

- **Calls and runs are exact counts.** A loop of 1000 shows 1000.
- **Times are shares**, and the seconds they are shares of. The report shows
  where to look, and a share is what stays the same when the machine changes.
- **`total`** includes everything the function called. **`self`** is the
  time spent on its own lines, including the builtins those lines call.
- **Waiting is its own row.** That covers a program blocked in `ui.run()`,
  `sleep`, `wait until` or a network read. A GUI program that waits 99% of the
  time must not report that its `ui.run()` line is "slow".

This is a report about the machine, not a value in the program. Matthew's
ruling that gBASIC time stops at seconds is about data. The profiler reads a
nanosecond clock inside the interpreter, as `sleep(0.25)` already does, and no
program sees it.

## 2. Measured before choosing

On this machine, today:

| | cost |
|---|---|
| one statement, interpreted (`s = s + i` in a `for` loop: 3,000,000 passes in 0.33 s, two statements a pass) | **about 55 ns** |
| `clock_gettime(CLOCK_MONOTONIC)` | **12 ns** |
| `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` | **122 ns** |

**Timing every statement exactly would cost too much and would distort the
report.** It needs two clock reads per statement, one on entering and one on
leaving. That is 24 ns on a 55 ns statement, so the tight loops would run
about 45% slower, and it is the tight loops the profiler exists to find. A
CPU clock is ten times worse.

## 3. The design: exact counts, sampled time

**Counts are counters.** Each statement node and each function carries a
count, incremented when it runs. An increment costs about 1 ns, it is exact,
and the counts tests can assert are exact.

**Time is sampled by a thread, not by a signal.** A sampler thread wakes
every 100 µs and reads three things the interpreter publishes:

- the statement now running;
- the stack of functions now active;
- whether it is waiting.

The sampler adds one sample to each. Its shares are statistical: a 2-second
run gives 20,000 samples, and a share's error is well under a percent.

**Why a thread and not `SIGPROF`:**

- A signal interrupts system calls. `poll` and `nanosleep` return `EINTR`
  even with `SA_RESTART`. The interpreter handles `EINTR` at 24 sites, but
  libpq, SQLite, libcurl and Qt are not ours. A profiler that changes what a
  program does under it is worse than none.
- A thread interrupts nothing. `src/modules/net.c` already runs a resolver
  thread, so linking against pthreads costs nothing new.
- `ITIMER_PROF` counts CPU time, which would hide waiting entirely. A thread
  sampling wall time can count waiting and report it as waiting.

**What the interpreter publishes:**
- **The current statement.** One relaxed atomic store in `eval_stmt`, made
  only while profiling. On x86-64 and ARM a relaxed store is a plain store.
- **The function stack.** A fixed array of function pointers and a depth,
  pushed and popped in `invoke_function`. Watch bodies, window handlers and
  server handlers all arrive through it.
- **The waiting flag.** It is set around the event loop's `poll` (which is
  also how `wait until` and `ui.run()` wait), `sleep`, and an actor's
  `receive`. It is not set for a blocking read in a module such as
  `sqlite.query`, which is time the program asked for (§9 Q3, ruled: the
  program's).

**How the hooks cost nothing when off.** `eval_stmt` and `invoke_function`
each begin with one test of `gb_profiling`. When it is set, the first pass
goes round a wrapper (`eval_stmt_profiled`, `invoke_function_profiled`) that
counts, publishes, and calls back in; the second pass carries on. So the
unprofiled path has no extra call and no extra frame.

**The sampler allocates nothing and takes no lock.** A web pool worker or an
actor is a fork without exec, and a fork copies only the forking thread: a
lock held by another thread at that moment would stay held in the child
forever. The sampler blocks every signal, so a process-directed signal still
interrupts the main thread's `poll`, as the webserver's drain needs.

**Attribution:**
- **A sample goes to the innermost statement.** A loop's own line keeps the
  samples taken between its body's statements (stepping and testing the
  counter), because the wrapper puts the caller's statement back on return. Time inside a builtin such as
  `sort` or `pg.query` goes to the line that called it, because that is the
  line the author can change.
- **For functions,** the top of the stack gets `self` and every distinct
  function on the stack gets `total`. Counting each function once per sample
  keeps recursion from adding up past 100% (a stamp on the function: the last
  sample that counted it).
- **A sample while waiting** goes to the waiting row and to every function's
  total, and to no line and no self. A sample with no statement published
  (before the first statement, between `main` and the event loop) is counted
  as "outside any line", and the header says so from 0.1%.
- **Lines are keyed by file and line.** A one-line `if c then x = 1` is two
  statements on one line. The line counts the most either ran, which is the
  times the line was reached: summing would count it again for each `then`
  taken (measured on `fib`, where it gave 2960 runs for 1973 calls). A function's file is the one it
  was parsed from, already stamped for error positions.

## 4. When it is off

**The target is no measurable cost.** Every hook sits behind
`if (gb_profiling)`, a global the branch predictor learns at once:
- one test in `eval_stmt`;
- one test in `invoke_function`;
- one test at each waiting site.

When profiling is on, `eval_stmt` runs through a wrapper that saves and
restores the published statement. The interpreter has no `longjmp`, so every
exit passes back through the wrapper.

**This is measured, not claimed:** the suite builds a second copy with the
hooks compiled out (`make gbasic-noprof`, `-DGB_NO_PROFILER`) and counts both
in **instructions** with callgrind, held to at most 1.01. Wall time was tried
first and could not hold a 3% bound: on a loaded machine this binary timed
against its own copy ranged from 0.81 to 1.01 (2026-11-21), while an
instruction count repeats exactly. Increment 1 measured 0.9994 off.
Increment 2 measures **1.0044**: of its 5.0 million extra instructions over
200,000 passes, 1.8 million are `eval_stmt`'s test (2 a statement) and 0.4
million `invoke_function`'s, and 2.8 million are `eval_user_function_bound`,
which holds no hook and compiled differently (its stack frame grew 64 bytes:
GCC's inlining in a 50,000-line file moved). Marking the wrappers cold and
never inlined changed nothing, so they are not marked.

Profiling on measured 1.003 to 1.047 in increment 1, and 0.98 in increment 2,
by where the program's file sits: the note copies its path, and a path of
another length leaves malloc's free lists in another state (`_int_malloc` 24.4
against 39.8 million instructions, the same program). Off, the hooks allocate
nothing, so that bound is exact.

**The overhead when on is not printed.** Measuring it would need the clock
reads §2 rules out. The suite holds it in instructions instead.

## 5. Where the report goes

The report goes to **standard error, after the program ends**. The program's
standard output is byte-identical with and without `--profile`, and a tier
holds it so.

It is written on:
- the normal end;
- an uncaught raise, after the error line;
- `end`;
- Ctrl-C, SIGTERM and SIGHUP, and the process then dies by the same signal.

**The signal path had to be built.** This note first said a death-by-signal
path was already in place; there was none. The default action simply killed
the process. Increment 2 installs a handler for each of the three signals,
only where the default would have killed: an ignored signal (`nohup`) or one
a server owns for its drain stays as it was, and a server's drain ends
normally and writes the report then.

- **The handler only records the signal.** Writing a report is not safe from a
  handler (it allocates, sorts, and writes through stdio).
- **The report is written by whichever thread claims it first:**
  - the main thread, at its next statement, call or end of a wait;
  - the sampler, at once while the main thread waits, or after **200 ms**
    while the main thread is inside one C call (a read of a terminal or a
    pipe, a long `sort`) and so meets no statement. Inside a C call it is not
    touching the profile's lists, which only statements change.
- **The loser parks** and the winner kills the process by the signal.
- **A second signal kills at once**, as the default would, with no report.
- **Counts only (`GBASIC_PROFILE_HZ=0`) installs no handler.** With no sampler,
  a main thread blocked in a read with `SA_RESTART` would never come back to
  act on the signal.

Known limits: a report written while the main thread is blocked writing to a
full pipe would wait on that pipe's lock, and a second signal is then the way
out. Windows has no signal path. A `SIGKILL` writes nothing.

## 6. What it does not do

- **Other processes.** An actor or a `process.start` child is its own
  process, and a web pool's worker or a G-SQL reader is a fork that has no
  sampler thread. Each could write its own report (§9 Q4). Increments 1 and 2
  profile the process they were started in. A forked child inherits the
  counters and writes nothing; the report does not say children ran.
- **Memory.** Time only. Allocation profiling is another tool.
- **A flame graph.** The stacks sampled are enough to write one (§9 Q2), and
  the counts alone are not.

## 7. The suite: `tests/run_profile.sh`

This section is the plan as written. What was built is in the suite's header,
and differs where measuring showed the plan could not hold: the signal tier
covers seven cases rather than one, the helgrind tier needed fair scheduling
(below), and two of the perturbations needed checks added first.

- **counts** (exact):
  - a loop of 1000 shows its body line at 1000;
  - a function called 37 times shows 37;
  - `fib(15)` shows 1973 calls;
  - a watch body, a library's line under the library's file, and a one-line
    `if`'s two statements summed.
- **shares** (ratios, which normalise themselves):
  - two functions with the same body, run 3N and N times, show `self` in a
    ratio between 2.5 and 3.5;
  - `total` is at least `self` everywhere;
  - `main`'s total is 100% within one sample.
- **waiting:** a program that sleeps 2 seconds and computes for a fraction of
  one shows waiting of at least 90%, and its `sleep` line low.
- **unchanged:**
  - standard output byte-identical with and without `--profile`, over a
    corpus of examples;
  - the exit code unchanged, including after a raise.
- **signal:** a profiled server sent SIGTERM writes its report and still dies
  by SIGTERM (python3 reads the status, as `run_lock_signal.sh` does).
- **off:** the instruction ratio of §4. The second copy is built into a
  temporary directory, never over `./gbasic`.
- **valgrind** and **helgrind.** Every published value is atomic. Helgrind
  does not model C11 atomics, so `tests/profile_helgrind.supp` names the
  functions making those accesses, by first frame only. **And the sampler must
  run:** under valgrind's default scheduler it took 3 samples in 11.78 s
  beside a computing interpreter, and a plain read planted in it went
  unseen. With `--fair-sched=yes` it takes about 6000, the planted race is
  reported, and the tier refuses a run of fewer than 1000.
- **perturbations**, each of which turned the suite red (2026-11-21):
  - a sample given to the outermost line instead of the innermost one (after
    the innermost-line check was added; before it, nothing held lines);
  - a count incremented twice on a statement's first run (every count one
    over);
  - the waiting flag left unset in `sleep` (waiting, the sleep line, signal);
  - recursion counted per frame (fib's total 1422.6%, after a recursion
    fixture was added; the shares fixture does not recurse);
  - the sampler never claiming while the main thread is inside a C call
    (signal);
  - no signal handlers (signal);
  - a plain, non-atomic read in the sampler (helgrind, after fair scheduling).

## 8. Increments

1. **Counts** (shipped). `--profile` with exact calls and runs, no thread. This alone
   finds a hot loop by counts. It includes the off ratio, the
   standard-output tier and valgrind.
2. **Time** (shipped). The sampler thread, shares, waiting, the signal paths
   and helgrind.
3. **What Matthew rules in §9:** JSON, builtins as their own rows,
   flame-graph stacks, children.

The hooks sit at the boundaries a compiled backend has too: statement entry,
function entry and exit, and the waiting sites. The LLVM backend can emit the
same calls, and the report would read the same.

## 9. Questions for Matthew (ruled 2026-11-21: all six as recommended)

1. **The flag's name and the report's destination.** `--profile` to standard
   error, plus `--profile=path` for a file? I recommend both.
2. **A machine-readable form** for Studio to draw in its gutter: `--profile-json`,
   or `--profile=path.json` chosen by the extension? I recommend the
   extension.
3. **A database's time.** Is time inside `pg.query` or `sqlite.query` the
   program's, shown on its line, or waiting? I recommend the program's: it is
   time the program chose to spend, and its line is where the author can act.
4. **Children.** Should an actor, a pool worker or a G-SQL owner profile too,
   each writing `<path>.<pid>`? I recommend leaving them out of increment 1.
5. **Builtins as rows.** Should `sort` or `pg.query` get their own entry in the
   functions table as well as on their line? I recommend yes, in
   increment 3: "40% in `json_decode`" is often the whole answer.
6. **The sample interval.** I recommend 100 µs by default, adjustable with
   `GBASIC_PROFILE_HZ`.
