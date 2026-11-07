# A library's own loads are its own

**Status: shipped 2026-11-07.** Asked for by Transward (its §7.6). Matthew
chose the option of making a library's loads private to it, and approved this
design as written.

## 1. The problem

`stdlib/mcp.bas` does `load tools from "tools.bas"`. Library names live in one
flat table for the whole program (`library_names`, `src/eval.c`), so that load
claims the name `tools` for everyone. Transward had its own `library tools`,
and loading `mcp` beside it was refused:

```text
the name 'tools' already refers to library 'tools' from .../transward/src/tools.bas,
and cannot also refer to library 'tools' from .../stdlib/tools.bas
```

The message is accurate. The defect is that an application cannot know which
names a stdlib library claims inside itself. A later stdlib release that adds
a `load` to some library can break an application that changed nothing.

## 2. How names resolve today

- Every function is registered with its library's **effective name**, the name
  qualification uses (the declared name, or the alias of `load X as y`).
- Running code knows its library as `current_function_library`, the same
  string.
- `lib.fn(...)` finds the function whose library string is `lib`. An
  unqualified call finds the running library's own function, then the root
  program's (run_library_scope.sh).
- `library_name_claim` refuses a second library claiming an effective name
  already taken, program-wide.

So a library is identified by the name it was loaded under, and that name is
the same everywhere.

## 3. The design: an identity, and a map per importer

1. **Every loaded library gets an internal identity.** When its effective
   name is free program-wide, the identity IS that name, as today. Only when
   the name is already taken by a different library, imported by a different
   file, does the newcomer get a hidden identity (`tools#2`). This is the case
   refused today and the only case that changes.
2. **Every importing scope keeps a map** from the names it loaded to
   identities. A scope is the root program, or one library's identity.
3. **A qualified call is resolved through the calling scope's map.** Inside
   `mcp`, `tools.dispatch` means the `tools` that mcp loaded. In Transward's
   program, `tools.x` means Transward's own. When the calling scope did not
   load the name, resolution falls back to the program-wide table, as today,
   so a library still reaches a library its caller loaded. That is how things
   work now, and changing it is a separate question.
4. **The refusal stays for one scope.** One file claiming one name for two
   libraries is still refused, with today's message and its `as` remedy.
   Across scopes there is no collision.
5. **Messages say the written name, never the identity.** A trace says
   `tools.dispatch`, and `library_collisions()` reports what it reports today.
   An identity like `tools#2` never reaches a user.

## 4. What this does not change

- Any program that runs today runs the same, since every name that is free
  program-wide keeps its identity. The whole gate is the check.
- A library's unqualified calls still reach its own functions first.
- `load X as y` is unchanged: an alias is still the importing file's name.

## 5. What it touches

- Function and modifier registration: the library string becomes the
  identity.
- The qualified-call and modifier lookups: the scope's map first, then the
  program-wide table.
- The library frames, which hold each library's top-level state and are keyed
  by name: keyed by identity.
- The name table and its refusal: per scope, plus the identity allocation.
- Every user-visible name: traces, `library_collisions()`, `reflect`,
  `--add-loads` hints and the LSP outline, which show the written name.

## 6. How it would be tested

A new tier in run_alias.sh or its own suite:
- Transward's exact case: a program with its own `library tools` loads
  `mcp`, and both work. `tools.x` in the program reaches the program's;
  `mcp.handle` reaches stdlib's.
- Two libraries each loading a different `util`, each reaching its own.
- One file claiming one name twice is still refused, with today's message.
- A trace from inside the hidden library names it as written.
- A program whose names are all free behaves exactly as before (the whole
  gate).

Each would be perturbed: resolve through the program-wide table only (the
Transward case goes red), and drop the per-scope refusal (the one-file case
goes red).

## 7. As built (2026-11-07)

As designed, with two details the design did not name:

- **A claim can be asked before it is made.** A load checks "already
  imported" and "a cycle" before the file is read, and both need the identity,
  while the claim itself must wait until the library is known to exist. So
  `library_name_peek` computes the identity a claim would give, records
  nothing and raises nothing.
- **A function value keeps its identity.** Only a WRITTEN qualifier is
  translated through the calling scope (a call, `lib.fn` read as a value, the
  discarded-result warning, a qualified modifier). A function value carries
  its identity, so a value made from alpha's `util` calls alpha's from
  anywhere. Translating inside `function_resolve` would have sent a stdlib
  function value named `tools` to an application's `tools`.

**Tested** by run_alias.sh's private tier (tests/private_loads/): alpha,
beta and the program each with its own `util`, a function value, a trace and
library_collisions() naming them as written (9 checks, and under valgrind);
one file claiming one name twice still refused; and Transward's own case, an
application's `library tools` beside stdlib's mcp. On the commit before, the
first was refused at alpha's own `load util`. Perturbed, each red: resolution
through the program-wide table only, the per-scope refusal removed, a hidden
identity shown raw. Every suite in the gate passed with the change, which is
the check that no program that ran before changed.

