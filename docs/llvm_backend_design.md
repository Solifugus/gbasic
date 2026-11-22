# Compiling gBASIC with LLVM: a design note

**Status:** In progress. Matthew ruled all six questions (§8) as recommended
on 2026-11-21. Increment 1 has begun: builtins, module verbs and operators
are resolved (§4, "Increment 1, as built"); variables are next. It follows his
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
