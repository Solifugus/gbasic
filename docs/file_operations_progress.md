# File Operations Progress

Last verified: 2026-06-06

## Status

Phases 1 and 2 are **complete**.

Implemented core functions:

- `delete(f)`
  - requires a file value
  - deletes the referenced file
  - returns `true` on success
  - raises a file-operation runtime error on failure
- `copy(f, target)`
  - requires a file-value source
  - accepts a file value or string path as the target
  - copies file contents and preserves permission bits when available
  - returns `true` on success
  - raises a file-operation runtime error on failure
- `move(f, target)`
  - requires a file-value source
  - accepts a file value or string path as the target
  - uses the platform rename operation, with copy-and-delete fallback across
    filesystems
  - returns `true` on success
  - raises a file-operation runtime error on failure
- `list_files(path)`
  - accepts a directory value or string path
  - returns an array of file values for regular files
  - sorts returned paths lexicographically for deterministic ordering
  - raises a file-operation runtime error when the directory cannot be listed

Existing `list(folder)`, `files(folder)`, and `folders(folder)` behavior was not
changed.

Phase 2 core functions:

- `make_dir(path)`
  - accepts a string, file reference, or directory reference
  - creates one directory with normal process permissions and umask
  - does not create missing parent directories
  - returns `true` on success
  - raises a file-operation runtime error if creation fails, including when the
    path already exists
- `remove_dir(path)`
  - accepts a string, file reference, or directory reference
  - removes one empty directory
  - does not recursively remove contents
  - returns `true` on success
  - raises a file-operation runtime error when the directory is missing,
    non-empty, or cannot otherwise be removed

## Files Changed

- `src/eval.c`
  - added Phase 1 filesystem operations and direct call dispatch
  - added copy, target-path, and deterministic file-list helpers
- `src/builtins.c`
  - registered the four functions as always-available core functions
- `examples/file_management_test.gb`
  - added repeatable positive coverage for delete, copy, move, and list_files
- `examples/file_management_test.out`
  - added expected positive output
- `examples/file_management_fixture/README.md`
  - keeps the test directory available without runtime directory creation
- `tests/negative_file_*.bas` and matching `.err` files
  - added type, missing-source, and invalid-target coverage
- `tests/run_examples.sh`
  - registered the positive file-management example
- `tests/run_negative.sh`
  - registered the new negative cases
- `docs/file_operations_progress.md`
  - records Phase 1 and Phase 2 implementation and verification

Phase 2 files:

- `src/eval.c`
  - added typed path handling and non-recursive `mkdir`/`rmdir` operations
- `src/builtins.c`
  - registered `make_dir` and `remove_dir` as core functions
- `examples/directory_management_test.gb`
  - added positive string-path and directory-value coverage
- `examples/directory_management_test.out`
  - added expected output
- `tests/negative_make_dir_*.bas`, `tests/negative_remove_dir_*.bas`, and
  matching `.err` files
  - added Phase 2 failure coverage
- `tests/run_examples.sh` and `tests/run_negative.sh`
  - registered the Phase 2 tests

## Tests Added

Positive coverage verifies:

- copying to a file-value target
- moving to a string-path target
- copied and moved file contents
- source removal after move
- listing through a directory value
- file-value entries from `list_files`
- deterministic path ordering
- successful deletion and post-delete absence
- repeatable fixture cleanup

Negative coverage verifies:

- invalid `delete` argument type
- invalid `copy` target type
- invalid `move` source type
- invalid `list_files` argument type
- missing source for `copy`
- missing source for `move`
- invalid target path for `copy`
- invalid target path for `move`

Phase 2 positive coverage verifies:

- creating a directory from a string path
- removing an empty directory through a directory value
- creating a directory through a directory value
- removing an empty directory from a string path
- repeatable cleanup with no directory left after success

Phase 2 negative coverage verifies:

- invalid `make_dir` argument type
- invalid `remove_dir` argument type
- removing a missing directory
- removing a non-empty directory without recursive deletion
- creating a directory at an existing path

## Verification

Commands run on 2026-06-06:

- `make clean && make`: passed without warnings.
- `./tests/run_examples.sh`: passed, including
  `examples/file_management_test.gb`.
- `./tests/run_negative.sh`: passed, including all eight new file-operation
  negative cases.

Phase 2 verification on 2026-06-06:

- `make clean && make`: passed without warnings.
- `./tests/run_examples.sh`: passed, including
  `examples/directory_management_test.gb`.
- `./tests/run_negative.sh`: passed, including all five Phase 2 negative cases.

## Next Recommended Phase

Define Phase 3 semantics and tests before implementation. The next coherent
phase is explicit overwrite policy and line-oriented reads. No `overwrite()` or
`read_lines()` functionality was implemented in Phase 2.
