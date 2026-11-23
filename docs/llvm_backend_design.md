# Compiling gBASIC with LLVM: a design note

**Status:** In progress. Matthew ruled all six questions (§8) as recommended
on 2026-11-21. Increment 1 is built (§4, "Increment 1, as built"): builtins,
module verbs, operators, variables and the function a call reaches are
resolved, and the whole program makes no `strcmp` per pass in the three
programs `tests/run_resolve.sh` measures. Increment 2, the baseline JIT, is
built in its three steps (§4, "Increment 2, as built so far"): step 2
measured no faster than the interpreter, which is what the finding in §3
predicts, and step 3 10-12% faster on loops; its shape is in §4,
"Increment 2, its shape". It follows his
direction of 2026-08-20 (an LLVM backend compiling gBASIC to machine code),
sharpened 2026-11-03 to "actual code generation by gBASIC 1.0"; typed
variables (`x:number`) were designed with that aim. The costs below were
measured before anything was proposed.

## 1. What it is for

**Programs that run faster, and that behave the same.** The interpreter's
behaviour is the specification: 238 suites, their goldens and the corpora
are the oracle a compiled program must match, output for output.

## 2. Measured first

On this machine, 2026-11-21, `./gbasic` at -O2.

**How fast the interpreter is**, against Python 3.14 and C (gcc -O2):

| | gBASIC | Python | C |
|---|---|---|---|
| `s = s + i * 2`, 10,000,000 passes | 1.41 s | 0.50 s | 0.023 s |
| `fib(30)`, 1.35 million calls | 0.61 s | 0.06 s | 0.001 s |

gBASIC is about 3 to 10 times slower than CPython today.

**Where the instructions go** (callgrind, four small programs:
`loop` 1,000,000 passes of `s = s + i * 2`; `fib(22)`; `strings` builds,
joins and splits 100,000 strings; `records` builds and filters 50,000
records):

| | loop | fib | strings | records |
|---|---|---|---|---|
| all instructions | 2,457 M | 231 M | 1,960 M | 1,337 M |
| **`strcmp`** | **24.8%** | **14.0%** | **50.6%** | **45.0%** |
| of which: a variable looked up by name | 13.6% | 6.1% | 4.2% | 2.8% |
| of which: an operator compared as text | 12.5% | 7.4% | under 0.5% | 1.6% |
| of which: a builtin found by name | — | 1.6% | 49.4% | 41.8% |
| startup (the dynamic linker, 87 libraries) | 1.1% | 12.1% | 1.4% | 2.1% |

(The `of which` rows include each caller's call overhead, so they sum to
slightly more than `strcmp`'s own share.)

- **A builtin is found by comparing its name** against a chain of names:
  37.5 million comparisons for about 300,000 builtin calls in `strings`,
  about 125 a call, more for one late in the chain.
- **An operator is compared as text** (`binary_apply`, `eval_binary`): 14
  comparisons a pass of the loop.
- **A variable is found by name** in its frame and its parents.
- Startup is 10-20 ms, 60% of it binding symbols in 87 shared libraries.

**What LLVM costs and gives**, by a probe (a C program building one function
through LLVM's C API and running it with ORC's LLJIT):

| | |
|---|---|
| LLVM installed here | 20.1.8, with `llvm-20-dev` |
| `libLLVM.so` | 143 MB, one shared library |
| linking it | 57 MB more resident; no measurable start time |
| building and compiling one small function | 3 ms (32 ms the first time, from disk) |
| the loop above, as doubles in registers | **6 ms** (the interpreter: 1,410 ms) |
| targets built | x86-64, AArch64, RISC-V, SystemZ (s390x), and more: every port gBASIC has |

## 3. The finding

**Every compiler resolves names before it runs anything, and that alone
removes a quarter to a half of the interpreter's instructions.** A compiled
program cannot look a builtin up by name or compare `"+"` as text; it calls
the builtin and adds. So the first step of any backend is to resolve each
name once, at parse or first use: operators to codes, builtins to entries in
a table, variables to slots in a frame.

**That step is also an interpreter speed-up.** Done in the interpreter first,
it makes every program faster with no new dependency, and the compiler then
lowers an AST whose names are already resolved. The bound it can give, if
every `strcmp` and nothing else went: 1.33x for `loop`, 1.16x for `fib`,
2.0x for `strings`, 1.8x for `records`.

**The large gain is types.** The probe's 230x is for doubles held in
registers. The interpreter's 1,410 ms is spent on names, boxing every
intermediate value, and dispatching every node. A compiler that still boxes
every value removes the dispatch only. Typed variables are what let it keep
a number in a register.

**But a gBASIC number is not a double.** It is exact as an integer above
2^53 and exact as a written decimal (`run_exact.sh`,
`run_exact_decimal.sh`): `+ - *` stay exact, overflow degrades with warning
2112. A compiled `x:number` must keep those rules: an exact integer in a
register with an overflow check, falling back to the boxed value. That is
cheap (one checked add), and it is the design question that decides the
real speed-up, not LLVM.

## 4. The plan, in increments

1. **Resolve names, in the interpreter.** Operators become codes at parse
   time. Builtins become entries in a table, found once per call site and
   kept on the node, as `--profile` already classifies call sites. Variables
   become slots: the hardest of the three, because some things find a
   variable by name at run time (watchers, `reflect`, the prompt, library
   frames, `env_find` for a function value). Each of those must be kept or
   given the slot. The gate must stay green throughout, and the profiler
   measures the gain.
2. **A baseline compiler**, JIT, per function. A function whose statements
   are all supported is lowered to LLVM IR that calls the runtime for every
   operation, on boxed values, and run by ORC's LLJIT; any other function is
   interpreted, as now. It removes dispatch, and is where the compiler's
   shape (the runtime ABI, the error model, watchers, `on error`, the
   profiler's hooks) is settled. Off by default: `GBASIC_COMPILE=all` forces
   it on, and the whole gate runs that way as a second lane.
3. **Types.** Typed variables and parameters held unboxed, under the
   exactness rules above; loops over them in registers.
4. **Ahead of time.** `gbasic build prog.bas -o prog`: the same IR written as
   an object and linked against `libgbasic.a`, a program that starts
   without an interpreter.

### Increment 1, as built

- **Builtins and module verbs (2026-11-21).** Every name a call is tested
  against is in `src/call_names.h`, a sorted list; a call site looks its name
  (and its qualifier, `pg` in `pg.query`) up once by binary search and keeps
  the number on the node (`AstExpr.as.call.name_id`, `library_id`). The
  chain's `strcmp(expr->as.call.name, "len")` tests became `CALL_IS(expr,
  len)`, an integer compare, in the same order, so which builtin a name
  reaches is unchanged; 472 names, from the builtin chain and the verbs of
  every module (`ui`, `net`, `gsql`, `xlsx` and the rest). The registry's
  answer for the aggregate family (`sum`, `max`) is kept the same way. A node
  copied with another qualifier (a handle's method, `db.query()` reaching
  `gsql`) resets its qualifier's number.
- **Measured** (callgrind, the programs of §2): `strings` 1,960 M to
  1,000 M instructions (1.96x; the bound in §3 was 2.0x), `records` 1,337 M to
  782 M (1.71x; bound 1.8x); `loop` and `fib` unchanged, as expected, since
  their names are variables and operators.
- **Held by** `tests/run_resolve.sh`: the list sorted (a binary search over an
  unsorted list misses silently), no comparison of a call's name as text left
  in the source, and the dispatch's `strcmp` calls the same at 2,000 and 4,000
  passes. Three perturbations each turned it or `run_handle_methods` red: the
  list unsorted, the number not kept, and the qualifier's number not reset.

- **Operators (2026-11-21).** `op_code` reads an operator's code from its
  characters with a switch (no `strcmp`), once in each function that tests
  one, and the tests compare codes. Nothing new on the node: the text stays
  for messages, and the nodes the interpreter builds for itself need no care.
  `env_get` checks a name's first character before comparing it with
  `error` and `this`, the two names it asks about on every read.
- **Measured** against the interpreter before increment 1: `loop` 1.21x
  fewer instructions (the bound in §3 was 1.33x, the rest is variables),
  `fib` 1.10x, `strings` 2.00x, `records` 1.77x.
- **A lesson from the gate.** `run_profile`'s off tier (the profiler's hooks
  at most 1.01x the instructions of a build without them) failed at 1.0102
  after the first slice. The two binaries made the same 7,202,959 `strcmp`
  calls, at 21.83 and 21.22 instructions each: `env_get`'s literals sat at
  different alignments, and `strcmp`'s cost depends on where its strings
  lie. A count of instructions is exact, but a ratio between two binaries
  still carries layout; removing the `strcmp`s (1.0065 after) removes most
  of it.
- **Held by** `run_resolve`'s operators tier (all twenty operators against
  answers worked by hand) and its cost tier for `tests/resolve/loop.bas`.
  Perturbed: `>=` read as `<=` turned it red, and so did a `strcmp` hidden
  from the source scan behind a `volatile` pointer, which only the cost tier
  saw. A `strcmp` against a one-character literal is compiled inline by gcc,
  so no call is left to count; the source scan is what catches that one.

- **Variables, and the function a call reaches (2026-11-21).** Not slots, as
  §4 planned: interning gave the same result with less change. Every
  variable's name is held once (`name_intern`), and a frame's symbols, its
  `outer_reads`, an identifier (`AstExpr.key`), a parameter list and a `for`
  counter keep that one pointer, so a frame is searched by comparing
  pointers. The walk -- this frame, then its parents, the first that has the
  name -- is unchanged, so what a name means is unchanged, including a name
  that becomes local only when first assigned, which is what made slots
  hard. `error` and `this`, which a read asks about first, are interned
  before anything and compared by pointer. The function an unqualified call
  reaches (the calling library's own, else the program's), a qualified
  call's, and whether a name is a builtin are kept on the call while the
  function table and the library-name table are unchanged
  (`function_generation`): every call had scanned the table by name, and a
  builtin call twice.
- **Measured** against the interpreter before increment 1: `loop` 1.50x
  fewer instructions, `fib` 1.26x, `strings` 2.21x, `records` 1.91x -- past
  §3's bounds, which counted only `strcmp`'s own instructions and not the
  walks around them.
- **Held by** `run_resolve`'s cost tier, which now counts the WHOLE program's
  `strcmp` calls at 2,000 and 4,000 passes of three programs (builtins, an
  arithmetic loop, user and library functions with parameters): the same at
  both. One perturbation stayed green and is recorded rather than hidden:
  not moving the generation when a function is registered. No path was
  found that registers a function after a call site has run; at the prompt,
  each entry resets the library-name table, which moves the generation
  anyway.
- **Left:** a call through a function value (`f = area`, then `f(1, 1)`)
  still finds its function by the value's name on every call, because a
  function value carries its own copy of the name; and the profiler's off
  cost. `run_profile` holds the hooks to 1.01x the instructions of a build
  without them, and the hooks have not changed (about 7.4 million
  instructions on its bench: 4.5 a statement, 7 a call and 14 of layout in
  `eval_user_function_bound`), but the interpreter under them has shrunk
  from 1,041 to 788 million, so the same hooks now measure 1.0093x. A faster
  interpreter will fail a ratio that the hooks themselves never moved. Done
  2026-11-22: the bound is now on the hooks, 6 instructions a statement run
  and 25 a call (`docs/profiler_design.md`).

### Increment 2, its shape (written before any of it is built)

**What is compiled is the body loop, and only that.** `invoke_function`
binds the arguments, opens the error frame, pushes the call-stack slot and
runs the profiler's call hooks; then a `while (pc < count)` loop runs each
statement and decides, from the statement's `EvalResult`, whether to go on,
absorb a raise (`on error goto next` / `goto label`), jump (`goto`,
`gosub`, a bare `return` to a gosub), or leave. A compiled function replaces
that loop and nothing else, so everything around it (binding, locks on
parameters, defaults, the error frame's rule 2, the trace, `this`, the
library frame) is the interpreter's own code, run once per call as now.

**The compiled body is a C function** `void body(EvalResult *out)`, built as
LLVM IR by `src/jit.c` (the only file that includes LLVM's headers) and run
by ORC's LLJIT. The interpreter calls it through `gb_jit_body(stmt)`, which
answers the compiled entry or NULL; NULL means the loop runs as now.

**The runtime it calls** is a set of `gbrt_*` functions in `src/eval.c`,
non-static, given to the JIT as absolute symbols (`LLVMOrcAbsoluteSymbols`)
so no `-rdynamic` and no symbol lookup by the dynamic linker. Values are
`Value`s on the compiled function's stack, passed by pointer; the IR never
looks inside one in this increment (`Value`'s layout is the interpreter's,
and increment 3 is where unboxed numbers come in).

**Built in three steps, each with the gate's second lane green:**

1. **Every statement through the runtime.** The IR is the body loop
   unrolled: block `s_i` calls `gbrt_stmt(stmt_i, &r)` (which is
   `eval_stmt`) and then one call, `gbrt_after(loop, i)`, which
   does what the loop does after a statement and answers the index of the
   next statement (the body's count at its end) or -1 to leave; anything
   but `i + 1` goes to a `switch` on that index. The interpreter's loop
   calls the same two functions. Labels,
   `goto`, `gosub`, `on error goto` and `runtime_stopped` all go through
   that one function, so they cannot differ from the interpreter's. Every
   function compiles. It removes nothing yet; it is the plumbing, and the
   lane proves the plumbing against all 239 suites.
2. **Statements lowered.** `if`, `while`, `for ... to`, an assignment to a
   plain name, a bare call and `return` become IR control flow, with each
   statement still announcing itself (`gbrt_enter(stmt)`: the profiler's
   statement hook and `current_line`/`current_column`, exactly eval_stmt's
   first lines) and every expression evaluated by `gbrt_eval(expr, &v)`.
   Loops in IR carry the interpreter's checks (interrupt, `break x`,
   `continue x`, the `for` counter's exactness).
3. **Expressions lowered.** Identifiers by their kept key, literals as
   constants built once, binary operators by `op_code`, calls with the
   function or builtin already resolved (increment 1's caches): each a
   `gbrt_*` call on stack `Value`s, so the tree walk and its dispatch are
   gone.

**What decides whether a function compiles:** in step 1, everything does.
From step 2, a statement kind not yet lowered stays a `gbrt_stmt` call
inside an otherwise compiled body, so a function is never refused for one
statement. `GBASIC_COMPILE=report` prints each compiled function with how
many of its statements were lowered, so coverage is a number (§5).

**Switches:** `GBASIC_COMPILE` unset or `0`: nothing compiled, `gbasic` as
today. `all`: every function, on its first call. `report`: as `all`, plus
the coverage lines on stderr at exit. A build without LLVM (`HAVE_LLVM=0`)
accepts the variable and compiles nothing, saying so once under `report`.

**Build:** `llvm-config` 18 or later, found as Qt is (`HAVE_LLVM`,
`LLVM_AVAILABLE=0` to leave it out); its flags only on `src/jit.c`. The
codegen level is O0 in step 1 (compile time is the gate's time) and
measured before raising it.

**The lane:** `GBASIC_COMPILE=all ./tests/run_all.sh`, the whole gate
again with every function compiled. Output, exit status and stderr must
match byte for byte; a suite that cannot (one that counts instructions or
times the interpreter) says why and skips under the lane, by name. Both
lanes gate a push once the lane exists.

**Perturbations planned:** `gbrt_after` answering NEXT on a raise (an
`on error` skipped); a JUMP to the label's own index instead of the next
(a statement run twice); a lowered `for` without the exactness check; the
statement hook left out of a lowered statement (the profiler's counts).

**Not in `tools/deps.list` yet.** That list's library rows are `pkg-config`
probes, and LLVM is found by a versioned `llvm-config` (`llvm-config-20`
here, with no unversioned name); it joins the list, and `install.sh`'s
question, when compiling makes a program faster (step 2 or 3).

### Increment 2, as built so far

**Step 1** (2026-11-22, pushed): as planned above. LLVM is opened with
`dlopen` on the first compile rather than linked (linked, every process
peaked at 85 MB against 31 MB), so a program that compiles nothing never
maps it; loading it costs about 18 ms once (a one-line program: 0.018 s
interpreted, 0.036 s compiled).

**Step 2** (2026-11-22): `if`, `while` and `for ... to` lowered; assignment,
a bare call and `return` were NOT, because lowered they would still be one
runtime call each (their whole work is the expression, step 3), which
saves nothing over `gbrt_stmt`.

- **Shape.** A lowered statement is IR control flow around runtime calls:
  `gbrt_if` / `gbrt_if_done`; `gbrt_while_enter` / `_test` / `_next`;
  `gbrt_for_enter` / `_test` / `_next` over a `GbFor` (the exact counter,
  its scale, the double counter) on the compiled code's stack. The lists
  inside them are unrolled as the body is, over a `GbList`, each statement
  followed by `gbrt_list_after`.
- **One runtime, not two.** The interpreted `if`, `while` and `for` call the
  same functions after their first (`eval_stmt` has already announced the
  statement), and `eval_stmt_list` calls `gbrt_list_begin`, `gbrt_list_stmt`
  and `gbrt_list_after`, as invoke_function's loop calls `gbrt_stmt` and
  `gbrt_after` since step 1. The first draft of step 2 copied the `if` and
  `while` logic into its runtime functions instead; it passed, and was
  replaced before the `for`, whose exact-counter rules were too much to
  hold in two places.
- **`--profile`.** The first call of a lowered statement runs the whole
  statement through `eval_stmt` when profiling, so the profiler times it as
  it times any other; profiling measures the interpreter.
- **The report** counts the statements the compiled code runs itself (the
  body and every list inside a lowered statement) and how many are lowered:
  `tests/jit/flow.report`, 19 functions, 154 statements, 40 lowered.
- **Held by** `run_jit`'s flow tier (every path out of a lowered `if`,
  `while` and `for`, compiled against interpreted byte for byte) and the
  gate's compiled lane. Perturbations proven red: a `continue` ending the
  lowered `while`, the IR's `if` branches swapped, an absorbed raise in a
  nested list re-running its statement, the `while`'s `loop_depth` dropped,
  the compiled `for` never stepping, the compiled `for` leaving after one
  pass, and a named `continue` claimed by the inner `for`.
- **Measured, best of five, this machine, otherwise idle:**

  | | interpreted | compiled |
  |---|---|---|
  | `loop` (10,000,000 passes of `s = s + i * 2` in a `for`, in a function) | 1.22 s | 1.21 s |
  | `while` with an `if`, 3,000,000 passes | 1.14 s | 1.15 s |
  | `fib(30)` | 0.57 s | 0.58 s |

  No difference: dispatching statements was never where the time went
  (§3). Step 3 removes the expression walk; increment 3 removes the boxing,
  which is where §3 puts the large gain.
- **How much of the compiled lane compiles** (a tally of every process the
  lane started, step 1's binary, 2026-11-22): 5,719 processes; 1,585 ran
  compiled code; 4,129 (72%) called no function at all, their code being at
  the top level or in a `program` block, which are not compiled. 19,859
  bodies compiled and 32 fell back to the interpreter, all in the
  packaging suite's copied binary, which cannot reach LLVM; 36.4 million
  calls ran compiled against 87 interpreted. So the lane holds the compiled
  code to the interpreter wherever a function is called, and a `program`
  block's own statements are a gap (below).
- **Found on the way** (DOGFOOD, open items 10 and 11, each with a
  `run_limitations` probe): a `gosub` inside an `if`, `while` or `for`
  returns past the whole statement, and a body assigning its `for` counter
  does not change the next pass. Both are the interpreter's behaviour, so
  the compiled code keeps them; each is a language question.
- **Not lowered yet:** `do ... until`, `for each`, `consider`, `with`.

**The program block and the top level** (2026-11-23): compiled as whole
lists (`gb_jit_list`), as step 2 compiles an `if`'s branch, so the 72% of
the lane's processes that call no function run compiled code too. Not
under `--profile` (every lowered statement is interpreted there, so it would
add only LLVM's start-up to the profiled run), and not at the prompt (a
compile per line typed). `tests/jit/top.bas` holds the top level and
`tests/jit/stray.bas` a program block's result (a named `continue` escaping
every loop, raised from the result: dropping the compiled list's result was
green until it was added).

- **What it costs every process now:** LLVM mapped and a list compiled. Peak
  memory: about 80 MB more (`run_read_at`'s chunked reader, 95 MB against
  14.8 MB interpreted), which is why that suite's memory ratio is now
  counted interpreted in both lanes, as `run_profile`'s off tier is (its
  instructions moved with the heap's layout once LLVM had shaped it). The
  lane took 1,893 s against 1,829 s.

**Step 3** (2026-11-23): expressions, a plain assignment and `return
expression` lowered.

- **Shape.** Each expression node by the shape `eval.c` gives it
  (`gbrt_expr_shape`, so `jit.c` holds no rule of the language): a number
  literal is a constant made at compile time by `eval_expr` itself and
  stored word by word, with no call; an identifier is `gbrt_ident`; an
  arithmetic or comparison operator is `gbrt_binary_enter`, its left,
  `gbrt_binary_left` (a raise skips the right), its right,
  `gbrt_binary_apply`, which are the calls `eval_binary` itself now makes;
  `and`, `or`, the set operators and every other node are `gbrt_eval`. A
  plain assignment (`name = e`: no operator, modifier or declared lock) is
  `gbrt_assign_begin`, the value, `gbrt_assign_end`, and the interpreted
  assignment ends in the same `assign_finish`; `return e` likewise ends in
  `return_finish`. An `if`'s and a `while`'s condition is evaluated by the
  compiled code and read by `truth_of`, as `gbrt_truth` reads it; the
  error mark each compares against is read from `error_generation` just
  before the expression, as the interpreted statement reads it.
- **Measured, best of five, this machine:**

  | | interpreted | compiled |
  |---|---|---|
  | `loop` (10,000,000 passes) | 1.20 s | 1.06 s |
  | `while` with an `if`, 3,000,000 passes | 0.83 s | 0.75 s |
  | `fib(30)` | 0.53 s | 0.51 s |

  As predicted before it was built: a pass of `loop` was ~1,830
  instructions, of which the walk itself (`eval_expr`, `op_code`, the
  statement dispatch) was at most a quarter; `binary_apply`, the variable
  reads and writes, `assign_lvalue` and `value_free` are the value work
  that increment 3 removes. Calls are not lowered, so `fib` gains least.
- **Report:** each body's line counts the expression nodes lowered too, the
  only place lowering shows, since output may not differ.
  `tests/jit/flow.report`: 25 bodies, 149 of 222 statements lowered, 251
  expressions.
- **Held by** flow.bas's step 3 functions (a raise in an operator's left
  operand skipping the right, one in the right, an undefined name, a locked
  name converting and refusing, a declared return type, a raise in a
  `while` condition, a raise in a `return`'s own expression), the same under
  the interpreter from before this step. Perturbations proven red: the
  right operand evaluated after a raise in the left; a constant stored one
  word short (hung, bounded); a `while` condition's error mark read after
  it; a `return`'s mark read after it (green until the raising return was
  added); constants not lowered (the report only). Passing a `return`'s
  mark as 0 stayed green and is all but equivalent: absorbing a raise puts
  `error_generation` back to the frame's entry, which is 0 in every frame
  these programs reach.

### Increment 3, its shape (a draft for Matthew; nothing of it is built)

**What is left after step 3 is the value work.** A pass of `loop`'s
`s = s + i * 2` was ~1,830 instructions under the interpreter, and step 3
removed at most a quarter: the rest is `binary_apply` checking both
operands' kinds and building a boxed result, the two variable reads, the
write (`assign_lvalue`, `env_set_key`, the watcher check) and `value_free`.
Increment 3 is a number held as a machine value, read and written with no
call: `i * 2` one multiply and an overflow check, `s = s + ...` one add.

**The rules it must keep (§3):** a number is exact as an integer (int64)
until it overflows, which degrades to the double with warning 2112; exact
as a written decimal (`0.1`), whose `+ - *` stay exact; a double from
division and functions. So a held number is a tag and a payload, an exact
int64 or a double; a decimal stays boxed, since its arithmetic is the
runtime's. An overflow, or any operation the compiled code does not do
itself, is a call to the runtime with the boxed values, the same function
the interpreter calls, so the slow path cannot differ.

**Where a held number lives.** In the compiled function's own slot, not
its `Symbol`. Nothing else can read a local (there are no closures, and a
call cannot see its caller's frame) except a statement of the same body
the compiled code does not lower, which reads the `Symbol`, and a `watch`
on it, which fires from the assignment. So, per function and per
variable, all or nothing: held only when every statement naming it is
lowered and nothing in the function watches it; written back to the
`Symbol` when the function leaves (a trace or the prompt may read it
after), otherwise never.

**Questions to rule before it is built:**

- **Q7. Which variables are held.** (a) Only those declared `x:number`
  and parameters `a:number`: simplest, and exact by declaration, but no
  `.bas` file in `stdlib` or `examples` declares one today, so it speeds
  up only code written for it. (b) Also a `for` counter and an untyped
  local whose every assignment in the function is a number by
  construction (a number literal, a `for` counter, arithmetic of held
  numbers, `len` and the other builtins answering a number). Anything
  else assigned to it (a parameter, a call's result, text) leaves it
  boxed. **Recommended: (b)**, decided once when the body is compiled,
  never by guessing at run time; a parameter becomes a candidate by
  declaring `:number`, which is what typed parameters were designed for.
- **Q8. A held number that a value of another kind would meet** (`s =
  s + name` where `name` is text) cannot happen under (b), since `name`
  being unheld makes `s` unheld. Under (a) it is the lock's conversion,
  already the rule. Nothing to rule unless (b) is extended to guess.
- **Q9. Whether the report and the lane are enough proof.** The lane
  compares every suite's output under the interpreter and compiled; the
  exactness suites (`run_exact`, `run_exact_decimal`) are where a held
  number would differ if it differed. Recommended: those two suites gain
  a tier that runs their arithmetic inside functions, so the compiled lane
  exercises held numbers on the same oracle (python3's integers and
  decimal), rather than at the top level only.

## 5. Correctness

- **The gate is the oracle.** Increment 2 adds a lane that runs every suite
  with `GBASIC_COMPILE=all`. A compiled program's output, exit status and
  stderr must match the interpreter's byte for byte.
- **Fallback is counted, not silent.** A function that cannot be compiled
  says why, under `--profile` or a `GBASIC_COMPILE=report` mode, so coverage
  is a number.
- **Perturbations,** as for the profiler: a wrong constant fold, an
  unchecked overflow, an `on error` skipped, each of which must turn the gate
  red.

## 6. What it would cost

- **A dependency:** `libLLVM` is 143 MB. Like Qt it would be optional
  (`HAVE_LLVM`, found by `llvm-config`), the lean build unchanged and the
  interpreter always there. `tools/deps.list`, `install.sh` and the
  libcurl-style floor checks would need an entry.
- **Its version:** LLVM's C API changes between releases. 20 is here, 18 on
  Ubuntu 24.04, 14 on 22.04. A floor must be chosen (Q3).
- **Time:** increment 1 is interpreter work in `src/eval.c`. Increments 2
  and 3 are new code of a size like the G-SQL SQL layer.

## 7. Alternatives considered

- **Emit C and run the system compiler.** No new dependency (every build
  machine has `cc`), it reaches every port, and gcc optimises as well. But it
  compiles in hundreds of milliseconds rather than milliseconds, needs a
  compiler on the machine that RUNS the program, and loads the result with
  `dlopen`. A fine route to ahead-of-time binaries, poor as a JIT.
- **A bytecode virtual machine.** It removes dispatch overhead partly and is
  portable, but it is a second execution engine to keep equal to the first,
  and it does not reach registers. Increment 1 gets most of what it would.
- **Do nothing but increment 1.** It is worth doing alone. It does not reach
  Matthew's aim.

## 8. Questions for Matthew, ruled

All six were ruled as recommended, 2026-11-21.

1. **Is increment 1 (resolving names in the interpreter) the first increment
   of the LLVM work?** I recommend yes. It removes 25-50% of the
   instructions measured above, every program gets faster, and a compiler
   needs it anyway.
2. **JIT first, or ahead-of-time binaries first?** I recommend the JIT
   (increment 2): `gbasic prog.bas` is unchanged, and the gate can run
   everything compiled. Ahead-of-time follows on the same IR.
3. **Which LLVM releases?** I recommend 18 and later (Ubuntu 24.04, Debian
   13, Fedora 40), with older systems building without it.
4. **When does a function compile?** I recommend on its first call when
   `GBASIC_COMPILE` asks, and automatic compiling (after N calls, as the
   profiler counts them) only once measured.
5. **A typed number's representation.** I recommend an exact 64-bit integer
   or a double in a register, with gBASIC's overflow rule as a check, and an
   exact decimal staying boxed.
6. **Which machines must pass the compiled lane?** I recommend x86-64 and the
   Orange Pi's riscv64 from the start; s390x and Windows when the compiler is
   whole.
