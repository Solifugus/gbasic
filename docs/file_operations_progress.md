# File Operations Progress

Last verified: 2026-06-06

## Status

Phases 1 through 5 are **complete**.

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

Phase 3 core function:

- `overwrite(f, text, pos)`
  - requires a file value, string text, and non-negative integer position
  - interprets position as a byte offset, consistent with `bytes(f)`
  - replaces bytes starting at the requested position without inserting or
    truncating
  - permits position equal to file size, which appends the text
  - rejects positions beyond end of file
  - permits empty text and leaves file contents unchanged
  - requires the file to already exist
  - returns `true` on success
  - raises a file-operation runtime error on validation or I/O failure

Existing `write()` and `append()` behavior was not changed.

Phase 4 core path functions:

- `join_path(a, b)`
  - accepts strings, file references, and directory references
  - returns a string
  - uses `/` separators consistently with the existing runtime
  - removes duplicate separators at the join boundary
- `file_name(path)`
  - returns the final path component as a string
- `directory_name(path)`
  - returns the directory portion as a string
  - returns `.` when the path has no directory component
- `extension(path)`
  - returns the final filename extension without the dot
  - returns an empty string for extensionless names and leading-dot files

The extraction functions ignore trailing separators. These functions manipulate
path text only and do not require the referenced path to exist.

Phase 5 core function:

- `read_lines(f)`
  - requires a file value
  - returns an array of strings
  - removes terminating `\n` and an optional preceding `\r`
  - preserves empty lines and all other whitespace
  - returns an unterminated final line
  - returns an empty array for an empty file
  - does not add a synthetic empty line after a terminating newline
  - raises a file-operation runtime error for invalid arguments or read failure

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

Phase 3 files:

- `src/eval.c`
  - added validated non-truncating positioned writes through `r+b`
- `src/builtins.c`
  - registered `overwrite` as a core function
- `examples/file_overwrite_test.gb`
  - added repeatable overwrite behavior and cleanup coverage
- `examples/file_overwrite_test.out`
  - added expected output
- `tests/negative_overwrite_*.bas` and matching `.err` files
  - added Phase 3 validation and missing-file coverage
- `tests/run_examples.sh` and `tests/run_negative.sh`
  - registered the Phase 3 tests

Phase 4 files:

- `src/eval.c`
  - added typed path conversion, join normalization, and component extraction
- `src/builtins.c`
  - registered the four path utilities as core functions
- `examples/path_utilities_test.gb`
  - added positive string, file-value, and directory-value coverage
- `examples/path_utilities_test.out`
  - added expected output
- `tests/negative_path_*.bas` and matching `.err` files
  - added type, missing-argument, and extra-argument coverage
- `tests/run_examples.sh` and `tests/run_negative.sh`
  - registered the Phase 4 tests

Phase 5 files:

- `src/eval.c`
  - added whole-file line splitting and `read_lines` file dispatch
- `src/builtins.c`
  - registered `read_lines` as a core function
- `examples/read_lines_test.gb`
  - added repeatable positive coverage and direct for-each use
- `examples/read_lines_test.out`
  - added expected output
- `tests/negative_read_lines_*.bas` and matching `.err` files
  - added type, missing-file, and argument-count coverage
- `tests/run_examples.sh` and `tests/run_negative.sh`
  - registered the Phase 5 tests

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

Phase 3 positive coverage verifies:

- replacement at an interior byte position
- preservation of trailing file contents
- empty text leaving the file unchanged
- position equal to `bytes(f)` appending text
- successful cleanup and post-delete absence

Phase 3 negative coverage verifies:

- invalid file argument
- non-string text
- negative position
- non-integer position
- position beyond end of file
- missing file

Phase 4 positive coverage verifies:

- joining paths with and without trailing or leading separators
- joining from a directory value
- root and empty-base joins
- filename extraction from strings and file values
- directory extraction from strings and file values
- final extension extraction from single-dot and multi-dot names
- empty extensions for extensionless names and leading-dot files

Phase 4 negative coverage verifies:

- invalid binary path argument types
- invalid unary path argument types
- missing binary arguments
- missing unary arguments
- extra binary arguments
- extra unary arguments

Phase 5 positive coverage verifies:

- normal newline-terminated text
- empty files returning an empty array
- preserved blank lines
- preserved leading and trailing spaces
- a final line without a terminating newline
- direct iteration with `for each`
- repeatable cleanup of generated files

Phase 5 negative coverage verifies:

- invalid file argument type
- missing file
- no arguments
- too many arguments

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

Phase 3 verification on 2026-06-06:

- `make clean && make`: passed without warnings.
- `./tests/run_examples.sh`: passed, including
  `examples/file_overwrite_test.gb`.
- `./tests/run_negative.sh`: passed, including all six Phase 3 negative cases.

Phase 4 verification on 2026-06-06:

- `make clean && make`: passed without warnings.
- `./tests/run_examples.sh`: passed, including
  `examples/path_utilities_test.gb`.
- `./tests/run_negative.sh`: passed, including all six Phase 4 negative cases.

Phase 5 verification on 2026-06-06:

- `make clean && make`: passed without warnings.
- `./tests/run_examples.sh`: passed, including
  `examples/read_lines_test.gb`.
- `./tests/run_negative.sh`: passed, including all four Phase 5 negative cases.

## Next Recommended Phase

Define Phase 6 semantics and tests before implementation. A coherent next phase
would address bounded or streaming file reads without changing `read_lines()`.
No recursive file operations, environment functions, database functions, or
SQLite support were implemented in Phase 5.
