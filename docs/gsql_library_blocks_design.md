# G-SQL: `database` and `sql` blocks inside a library

**Status:** Shipped 2026-11-02, held by `run_gsql_module.sh`'s library tier. Asked for by the gBASIC books' volume 3
(`gbasic-books/docs/volume-3/gap-ledger.md`, G3).

## 1. The defect, measured by reading the source

`library_import_from_block` registers a library's `load`s, functions and
exported modifiers and **ignores every other statement**. So a `database` or
`sql` block written inside `library store ... end library` parses, is accepted,
and is bound nowhere. The library's own functions then fail three different
ways, none naming the cause:

- `make.exec()` says *'make' does not define 'exec'*;
- `gsql.exec(make)` says *undefined variable: make*;
- `gsql.exec(store.make)` says *library 'store' has no function 'make'*.

The same blocks at a program's top level work, including from inside a
function. And in the program's OWN file a block inside a `library` was refused
by name, while in a loaded library file it was accepted: **a loaded library file
was never given the declaration checks at all** (`loaded_file_get_opt` parsed it
without `server_blocks_validate`), so a `server` or `window` block there went
unchecked too. Integration code is organized as libraries (one per supplier, one
for the warehouse), so blocks have to live in them.

## 2. What it does

A library's `database` and `sql` blocks belong to **the library**:

- **Its own functions reach them by bare name**, as a program's functions reach
  the program's: `make.exec()`, `for each row in cheap`, `gsql.exec(make)`.
- **A library's own name comes first.** A bare name inside a library function
  is looked up in the function's frame, then **the library's blocks**, then the
  program's globals. So a program with its own `make` cannot capture the
  library's, which is the precedence an unqualified call inside a library
  already has (`run_library_scope.sh`).
- **A block's database is its own library's.** `sql make on shop` inside
  `store` is on `store`'s `shop`, never on a `shop` the program declares. Two
  libraries may each declare `database shop`; they are two databases.
- **From outside, qualified:** `store.make` is the block, so a program can
  write `gsql.exec(store.make)` or `for each row in store.cheap`. A program
  normally calls the library's functions instead, but the qualified name is how
  a test reaches one statement.
- **`:name` parameters read where the block runs**, as they do today, so a block
  run from a library function reads that function's variables.

Nothing else about a block changes: the same `load gsql` requirement, the same
literal `file:`, the same roles (`database shop( )` with no file, given a
connection at use).

## 3. How

Each library gets a frame of its own, made when it is imported and kept for the
process, holding its `database` and `sql` bindings. A call to the library's
function makes the function's frame a child of the library's frame, whose
parent is the global frame, so the lookup order of §2 is the ordinary scope
chain and needs no new rule. Assignment is unchanged: it writes the function's
own frame, so a library function cannot rebind its library's blocks.

A statement records the frame its database is bound in, which replaces the
global-frame lookup in `gsql_block_connection`.

An actor child re-imports its libraries, so it registers the same blocks.

**A loaded library file now gets the checks a program's file gets**
(`gb_validate_declarations`): a syntax error in one of its `sql` blocks, an
`on` naming no database in the library, a non-literal option, a block nested
in one of its functions and a name declared twice are each refused at their
line in the library file, before anything runs. In a `library` written in the
program's own file the same blocks are checked as the library's scope.

## 3a. Evidence

`tests/gsql/libblocks/`: two libraries each declaring `database shop`, and a
program declaring its own `shop` and `make`, so a library function that reached
the program's block, or the other library's, answers wrongly rather than
passing (13 checks, and under valgrind). Five refusals, each located in the
library file. An alias (`load store as shelf`) is the same library under
another name: its functions reach the same blocks' statements over the same
file.

## 4. Not done

- **Warning 2121** (a block nothing runs) stays a program's. A library may
  declare a statement its caller never needs.
- **No library-level variables.** The frame holds declarations only, as a
  library body runs no statements.
