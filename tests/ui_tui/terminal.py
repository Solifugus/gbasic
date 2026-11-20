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


def tier_needs(gbasic, fixtures):
    prog = os.path.join(fixtures, "needs.bas")
    r = Run(gbasic, [prog], cols=40, rows=12, env=NO_DISPLAY)
    r.finish(10)
    check("this program's windows need `docking`, and no backend that can draw here has it -- it is qt's, which cannot draw here" in r.err(),
          "declared needs: the terminal, lacking `docking`, was skipped and the refusal names who has it: %r" % r.err())
    r.close()


def main():
    tier, gbasic, fixtures = sys.argv[1], sys.argv[2], sys.argv[3]
    {"live": tier_live, "needs": tier_needs}[tier](gbasic, fixtures)
    return 1 if drive.failures else 0


if __name__ == "__main__":
    sys.exit(main())
