#!/usr/bin/env python3
"""The tui backend in a real pseudo-terminal (tests/run_ui_tui.sh). The
oracle is this driver: it types what a person would, reads the pty's
settings itself, and, where pyte is importable, reads the screen back with a
terminal emulator that is not ours. Reuses tests/term/drive.py's Run.

usage: terminal.py TIER GBASIC FIXTURE_DIR"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "term"))
import drive  # noqa: E402
from drive import Run, check, fresh_settings, settings_equal  # noqa: E402

NO_DISPLAY = {"DISPLAY": "", "WAYLAND_DISPLAY": "", "QT_QPA_PLATFORM": "", "GBASIC_UI": "",
              "GBASIC_TUI_SCREEN": ""}


def tier_live(gbasic, fixtures):
    prog = os.path.join(fixtures, "live.bas")
    r = Run(gbasic, [prog], cols=40, rows=12, env=NO_DISPLAY)
    check(r.wait_err("backend tui", 10), "no display, a terminal: the selection rule chose tui")
    r.pump(0.4)
    check(b"\x1b[?1049h" in r.out and b"Who" in r.out, "the window was drawn on the alternate screen")
    r.send(b"hi there", 0.2)
    r.send(b"\t", 0.1)
    r.send(b" ", 0.1)
    r.send(b"\r", 0.3)
    check(r.wait_err("go: hi there sure=true", 5), "typed, Tab, Space on the check, Enter: the default button saw both")
    check(r.wait_err("after run, open true", 5), "ui.stop ended the run and left the window open (rule 5)")
    st = r.finish(10)
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "exit 0")
    tail = r.out[r.out.rfind(b"\x1b[?1049l"):] if b"\x1b[?1049l" in r.out else b""
    check(b"printed after the run" in tail, "the run's end left the alternate screen, so later output is on the ordinary one")
    check(settings_equal(r.settings(), fresh_settings()), "the pty's settings after equal a fresh pty's")
    try:
        import pyte
        screen = pyte.Screen(40, 12)
        pyte.ByteStream(screen).feed(r.out[:r.out.rfind(b"\x1b[?1049l")])
        d = screen.display
        check(d[0].strip() == "Ask" and d[1].startswith("Who hi there") and d[2].startswith("[x] Sure")
              and d[3].rstrip().endswith("[ Go ]"), "pyte read the last frame: %r" % d[:4])
    except ImportError:
        print("SKIP pyte (the screen itself was not read back)")
    r.close()

    r = Run(gbasic, [prog], cols=40, rows=12, env=NO_DISPLAY)
    r.wait_err("backend tui", 10)
    r.pump(0.4)
    r.send(b"\x11", 0.3)   # Ctrl-Q
    check(r.wait_err("closed\nafter run, open false", 5), "Ctrl-Q closed the window: the closed handler once, and the run ended")
    st = r.finish(10)
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "Ctrl-Q: exit 0")
    check(settings_equal(r.settings(), fresh_settings()), "Ctrl-Q: the pty's settings after equal a fresh pty's")
    r.close()

    env = dict(NO_DISPLAY)
    env["QT_QPA_PLATFORM"] = "offscreen"
    r = Run(gbasic, [prog], cols=40, rows=12, env=env)
    got = r.wait_err("backend ", 10)
    r.pump(0.5)
    check("backend qt" in r.err() or "backend tui" not in r.err(),
          "a display (Qt offscreen) as well as a terminal: the GUI first (%s)" % r.err().split("\n")[0])
    os.kill(r.pid, 9)
    r.finish(5)
    r.close()


def tier_live_parts(gbasic, fixtures):
    r = Run(gbasic, [os.path.join(fixtures, "live_parts.bas")], cols=40, rows=12, env=NO_DISPLAY)
    check(r.wait_err("backend tui", 10), "increment 4 live: the terminal was chosen")
    r.pump(0.5)
    # row 1 is the title, row 2 the menu bar, row 3 the table's headings, so
    # the table's second row (Bo) is the terminal's row 5 (1-based)
    r.send(b"\x1b[<0;2;5M\x1b[<0;2;5m", 0.2)
    check(r.wait_err("activated Bo", 5), "a press on the second row of the table activated that row (SGR mouse)")
    r.send(b"\x1bOP", 0.1)        # F1 is not the menu key
    r.send(b"\x1b[21~", 0.2)      # F10
    r.send(b"\r", 0.2)
    check(r.wait_err("hello pressed", 5), "F10 then Enter pressed the first item of the first menu")
    # the mouse: File's title on the bar (row 2), then Hello, the first entry
    # of the menu that drops below it (row 4: its box's border is row 3)
    before = r.err().count("hello pressed")
    r.send(b"\x1b[<0;3;2M\x1b[<0;3;2m", 0.2)
    r.send(b"\x1b[<0;4;4M\x1b[<0;4;4m", 0.3)
    check(r.wait_err("hello pressed\nhello pressed", 5) and r.err().count("hello pressed") == before + 1,
          "a press on File's title opened it, and a press on its first entry pressed Hello")
    r.send(b"\x1b[21~", 0.2)
    r.send(b"\x1b[B", 0.1)
    r.send(b"\r", 0.3)
    check(r.wait_err("quit pressed", 5), "F10, Down, Enter: the second item, which stops the run")
    st = r.finish(10)
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "increment 4 live: exit 0")
    check(settings_equal(r.settings(), fresh_settings()), "increment 4 live: the pty's settings after equal a fresh pty's")
    r.close()


def tier_live_dialogs(gbasic, fixtures):
    folder = os.environ["TUI_PICK"]
    r = Run(gbasic, [os.path.join(fixtures, "live_dialogs.bas"), folder], cols=40, rows=12, env=NO_DISPLAY)
    check(r.wait_err("backend tui", 10), "increment 5 live: the terminal was chosen")
    r.pump(0.5)
    r.send(b"\r", 0.3)               # Enter on Ask, which has the focus
    check(b"Sure?" in r.out and b"Yes" in r.out, "Enter on Ask put the question up in a box (its words; the redraw skips cells already blank)")
    r.send(b"\r", 0.3)               # No is the default
    check(r.wait_err("answer false", 5), "Enter at once answers No, the safe default")
    r.send(b"\r", 0.3)
    r.send(b"\x1b[D", 0.1)          # Left, to Yes
    r.send(b"\r", 0.3)
    check(r.wait_err("answer true", 5), "Left, then Enter, answers Yes")
    # Tell: a box 10 cells wide and 5 high, centred on 40 by 12, so its OK
    # button is the terminal's row 7, columns 18 to 23 (1-based)
    r.send(b"\t", 0.1)
    r.send(b"\r", 0.3)
    r.send(b"\x1b[<0;19;7M\x1b[<0;19;7m", 0.3)
    check(r.wait_err("told true", 5), "a mouse press on the OK button answered the statement")
    r.send(b"\t", 0.1)
    r.send(b"\r", 0.4)               # Pick
    check(b"a.txt" in r.out and b"b.txt" in r.out, "the chooser listed the folder")
    r.send(b"\x1b[B", 0.1)          # Down: a.txt (.. is first)
    r.send(b"\x1b[B", 0.1)          # Down: b.txt
    r.send(b"\r", 0.3)
    check(r.wait_err("picked PICK/b.txt", 5), "Down, Down, Enter chose the second file")
    r.send(b"\r", 0.4)
    r.send(b"\x1b", 0.5)            # a lone Escape: the decoder's wait decides it
    check(r.wait_err("picked nothing", 5), "a lone Escape cancelled the chooser: unknown")
    r.send(b"\t", 0.1)
    r.send(b"\r", 0.3)               # Quit
    st = r.finish(10)
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "increment 5 live: exit 0")
    check(settings_equal(r.settings(), fresh_settings()), "increment 5 live: the pty's settings after equal a fresh pty's")
    r.close()


def tier_live_editor(gbasic, fixtures):
    r = Run(gbasic, [os.path.join(fixtures, "live_editor.bas")], cols=40, rows=12, env=NO_DISPLAY)
    check(r.wait_err("backend tui", 10), "increment 6 live: the terminal was chosen")
    r.pump(0.5)
    # the memo has the focus: its frame's top is row 2, its text starts at
    # row 3, column 2 (1-based)
    r.send(b"ab\rcd", 0.3)                         # typed, Enter a new line
    r.send(b"\x1b[200~xy\nz\x1b[201~", 0.3)      # a bracketed paste
    r.send(b"\x1b[<0;3;3M\x1b[<0;3;3m", 0.2)      # a press between a and b
    r.send(b"Q", 0.2)
    # a press at the start of line 2, dragged two cells right: "cd"
    r.send(b"\x1b[<0;2;4M\x1b[<32;4;4M\x1b[<0;4;4m", 0.2)
    r.send(b"\x7f", 0.2)                            # Backspace takes the selection
    r.send(b"\x1b[Z", 0.2)                          # Shift+Tab leaves the memo, round to Done
    r.send(b"\r", 0.3)
    check(r.wait_err("text aQb|xy|z caret 2:1", 5),
          "typed, pasted with its line break, pressed into, dragged across and cut: %r" % r.err())
    st = r.finish(10)
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "increment 6 live: exit 0")
    check(settings_equal(r.settings(), fresh_settings()), "increment 6 live: the pty's settings after equal a fresh pty's")
    r.close()


def tier_needs(gbasic, fixtures):
    prog = os.path.join(fixtures, "needs.bas")
    r = Run(gbasic, [prog], cols=40, rows=12, env=NO_DISPLAY)
    r.finish(10)
    check("this program's windows need `docking`, and no backend that can draw here has it -- it is qt's, which cannot draw here" in r.err(),
          "declared needs: the terminal, lacking `docking`, was skipped and the refusal names who has it: %r" % r.err())
    r.close()


def main():
    tier, gbasic, fixtures = sys.argv[1], sys.argv[2], sys.argv[3]
    {"live": tier_live, "live_parts": tier_live_parts, "live_dialogs": tier_live_dialogs,
     "live_editor": tier_live_editor,
     "needs": tier_needs}[tier](gbasic, fixtures)
    return 1 if drive.failures else 0


if __name__ == "__main__":
    sys.exit(main())
