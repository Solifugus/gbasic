# From the Windows session to the Linux session (2026-11-14)

What the Windows port found this week that the Linux session should look at.
`docs/windows_port_status.md` §36 and §37 have the full record; this is the
short list of what needs a Linux machine, a decision, or both. Branch
`windows-port`.

## 1. Run on Linux with Qt: these changes were not

WSL here has no Qt, so nothing below was run on Linux with Qt.

- **A use-after-free in `gb_qt_blocks_set`, on every platform**
  (`src/modules/qt_backend.cpp`). When a block's content changed, the old
  block was deleted but stayed in `m_blocks`; the cursor moves that follow
  make the document lay out again, and `extraFor` read the freed block.
  `tests/qt/blocks_probe.cpp` crashed in 21 runs of 30 on Windows; glibc's
  heap had hidden it. Fixed by taking a replaced block out of `m_blocks`
  before freeing it, and switching `m_blocks` to the new set before freeing
  the leftovers. **Please run `run_qt_notebook` under valgrind on Linux**: it
  should be clean now, and it is worth knowing whether valgrind saw the old
  read (if not, the valgrind tier does not reach this path).
- **The Qt suites' "no display" runs now go through one helper**,
  `gb_no_display` in `tests/build_has.sh`, in run_qt_backend, chooser,
  clipboard, dialog, source, examples, lifecycle and modal. On Linux it does
  exactly what each copy did (unset DISPLAY, WAYLAND_DISPLAY,
  QT_QPA_PLATFORM, XDG_RUNTIME_DIR, XDG_SESSION_TYPE). Two sites differed
  slightly before: backend's theming tier set `QT_QPA_PLATFORM=` (empty)
  rather than unsetting it and kept XDG_SESSION_TYPE, and examples kept
  XDG_SESSION_TYPE. Worth one Linux run of each to confirm nothing moved.
- **`tests/qt_dialog_modal_fixture.bas` raises its dialog after its first
  `/ping`**, not on a fixed 0.4 s timer (the suite's control lost that race
  under MSYS2). Same assertions; please run `run_qt_dialog` on Linux.
- `src/modules/qt.c` undefines `near` (a `<windows.h>` macro) when defined;
  a no-op on Linux.

## 2. Asks from running gBASIC Studio on Windows

Studio's model and both headless UI driver modes gave byte-identical output
on Windows and Linux, at Windows paths. The window opened. Then Open Samples
opened nothing, because Studio decides "absolute" by a leading `/`
(gbasic-studio `docs/windows_findings.md`, branch `windows`). Each item here
is a gBASIC change that would let a program not write that code at all.
Matthew to decide which, and the names.

- **Is a path absolute?** gBASIC has the answer in C (`path_is_absolute`,
  `src/eval.c`: `C:\`, `C:/`, `\`, `/` on Windows) but not as a builtin.
  Studio wrote `left(p, 1) = "/"` in two places, and that one line is what
  stopped it.
- **A lexical normal form and "same file?"** Studio canonicalises paths by
  hand (`studio_docs._canonical`) to tell that two spellings are one open
  document, and the readiness doc counts four hand-written splitter sets.
  `real_path` cannot replace them, since it needs the file to exist. On
  Windows "same" also means case-insensitive.
- **`GBASIC_PATH` is split on `:`** (`src/main.c`, `src/eval.c`), so it cannot
  hold a Windows path (`C:\...`). `;` on Windows, as PATH does, is the
  obvious answer; Studio meanwhile passes a relative `lib`.
- **Line endings.** Git for Windows checked Studio out with CRLF (343 of 354
  text files), and every driver edit that replaced text containing `"\n"`
  matched nothing. `read` is right to hand the bytes through, as on Linux,
  but every program that splits on `"\n"` will meet `\r` on Windows, from
  git or Notepad. An opt-in way to read text with line endings normalised
  would save each program finding it.
- **Where per-user data goes.** Studio reads `HOME`, which PowerShell and cmd
  do not set. gBASIC answered this for itself (`%LOCALAPPDATA%\gbasic`) and
  could answer it for programs.

## 3. Open on Windows, for information

- `run_gsql_owner`'s `churn` tier fails on Windows: 1 to 7 of 800 opens at
  linger 0 take over 2 s, the slowest about 5 s. A trace shows chains of
  lost races, each costing a 200 to 400 ms owner start on two cores; none
  waits on a gone owner. Left failing, not loosened (§36).
- The full Windows sweep: 163 passed, 44 failed, 26 skipped, mostly suites
  that never ran on Windows before (paths handed across MSYS, symlinks,
  `/proc`, POSIX zones). §36 classifies them.
- On Linux WITHOUT Qt, `run_qt_examples` fails on `theme.bas` (exit 1), at
  4866e943 as well as now. Not investigated.

## 4. Changed in the harness, which Linux runs too

- `tests/portable.sh` supplies `pgrep -f` and `pkill -f` on MSYS2/Cygwin only
  (Win32_Process and taskkill). Linux is untouched.
- `tests/run_gsql_module.sh`: the lifetime tier runs its two programs
  `--line-buffered`, and `find_owners` accepts the Windows spelling of the work
  directory. Passed on Linux (WSL).
- `tests/gsql/owner_test.c` runs its children as roles on both platforms
  (§36). Passed every tier on Linux under ASan and UBSan.
