#!/usr/bin/env python3
"""Drives a gBASIC `term` program through a real pseudo-terminal
(tests/run_term.sh). THE ORACLE IS THIS DRIVER: it writes the bytes a
terminal would send, written by hand from xterm's ctlseqs, and holds what
comes back to what it wrote; it reads the pty's settings itself before and
after; and the screen tier hands the output to pyte, a terminal emulator
that is not ours.

usage: drive.py TIER GBASIC FIXTURE_DIR

Each tier prints PASS/FAIL lines and exits 1 on any failure, 77 to skip."""
import os
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import fcntl
import time

try:
    SCALE = max(1.0, float(os.environ.get("GATE_TIME_SCALE", "1") or "1"))
except ValueError:
    SCALE = 1.0

failures = 0


def check(ok, what):
    global failures
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


class Run:
    """One program under a pty, its stderr in a file (unless `stderr_tty`)."""

    def __init__(self, gbasic, args, cols=80, rows=24, stderr_tty=False, env=None, job=False):
        self.err_path = None
        if not stderr_tty:
            fd, self.err_path = tempfile.mkstemp(prefix="term-err-")
            os.close(fd)
        e = dict(os.environ)
        e.update({"TERM": "xterm-256color", "COLORTERM": ""})
        if env:
            e.update(env)
        pid, master = pty.fork()
        if pid == 0:
            # the size before the program can ask for it (set from the parent
            # after the fork, it raced the program's start)
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
            if self.err_path:
                fd = os.open(self.err_path, os.O_WRONLY | os.O_TRUNC)
                os.dup2(fd, 2)
            if job:
                # As a shell runs a job: the program in a process group of its
                # own, the terminal's foreground group, under a parent in the
                # same session. Without that its group is ORPHANED (pty.fork's
                # child leads the session), and the kernel discards a stop
                # signal sent to an orphaned group, so Ctrl-Z could not stop it.
                signal.signal(signal.SIGTTOU, signal.SIG_IGN)
                child = os.fork()
                if child == 0:
                    os.setpgid(0, 0)
                    os.tcsetpgrp(0, os.getpid())
                    signal.signal(signal.SIGTTOU, signal.SIG_DFL)
                    os.execve(gbasic, [gbasic] + args, e)
                os.setpgid(child, child)
                _, st = os.waitpid(child, 0)
                os._exit(os.waitstatus_to_exitcode(st) & 255 if os.WIFEXITED(st) else 128 + os.WTERMSIG(st))
            os.execve(gbasic, [gbasic] + args, e)
        self.pid = pid
        self.job = None
        if job:
            end = time.time() + 5
            while time.time() < end and self.job is None:
                try:
                    kids = open("/proc/%d/task/%d/children" % (pid, pid)).read().split()
                except OSError:
                    kids = []
                if kids:
                    self.job = int(kids[0])
                time.sleep(0.01)
        self.master = master
        self.out = b""
        self.status = None

    def settings(self):
        """The pty's line discipline settings, read on the master side."""
        return termios.tcgetattr(self.master)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], max(0, min(0.05, end - time.time())))
            if r:
                try:
                    data = os.read(self.master, 65536)
                except OSError:
                    return
                if not data:
                    return
                self.out += data

    def send(self, data, pause=0.05):
        os.write(self.master, data)
        self.pump(pause)

    def err(self):
        if not self.err_path:
            return ""
        with open(self.err_path, "rb") as f:
            return f.read().decode("utf-8", "replace")

    def wait_err(self, text, seconds=5.0):
        end = time.time() + seconds * SCALE
        while time.time() < end:
            if text in self.err():
                return True
            self.pump(0.02)
        return False

    def wait_out(self, data, seconds=5.0):
        end = time.time() + seconds * SCALE
        while time.time() < end:
            if data in self.out:
                return True
            self.pump(0.02)
        return False

    def finish(self, seconds=10.0):
        end = time.time() + seconds * SCALE
        while time.time() < end:
            self.pump(0.05)
            pid, st = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.status = st
                self.pump(0.1)
                return st
        os.kill(self.pid, signal.SIGKILL)
        _, st = os.waitpid(self.pid, 0)
        self.status = st
        return None

    def close(self):
        try:
            os.close(self.master)
        except OSError:
            pass
        if self.err_path:
            os.unlink(self.err_path)


def err_lines(run):
    return [l for l in run.err().split("\n") if l]


def tier_keys(gbasic, fixtures):
    r = Run(gbasic, [os.path.join(fixtures, "waited.bas")], cols=100, rows=30)
    check(r.wait_err("ready 100x30"), "the program saw the pty's size: 100x30")
    raw = r.settings()
    check(not (raw[3] & termios.ICANON) and not (raw[3] & termios.ECHO), "raw mode: ICANON and ECHO off")
    check(bool(raw[3] & termios.ISIG), "ISIG stays on, so Ctrl-C still interrupts")
    check(b"\x1b[?1049h" in r.out, "the alternate screen was entered")
    check(b"\x1b[?1006h" in r.out and b"\x1b[?2004h" in r.out, "SGR mouse and bracketed paste were asked for")
    # (bytes the terminal sends, the line the program must write)
    cases = [
        (b"a", "key a [a]"),
        (b"Z", "key Z [Z]"),
        (b" ", "key space [ ]"),
        ("é".encode(), "key é [é]"),
        ("中".encode(), "key 中 [中]"),
        (b"\r", "key enter []"),
        (b"\t", "key tab []"),
        (b"\x7f", "key backspace []"),
        (b"\x01", "key ctrl+a []"),
        (b"\x1b[A", "key up []"),
        (b"\x1bOB", "key down []"),
        (b"\x1b[1;5C", "key ctrl+right []"),
        (b"\x1b[1;2D", "key shift+left []"),
        (b"\x1b[3~", "key delete []"),
        (b"\x1b[5;3~", "key alt+page_up []"),
        (b"\x1bOP", "key f1 []"),
        (b"\x1b[24~", "key f12 []"),
        (b"\x1b[Z", "key shift+tab []"),
        (b"\x1bx", "key alt+x []"),
        (b"\x1b[<0;12;7M", "mouse press 1 12,7"),
        (b"\x1b[<0;12;7m", "mouse release 1 12,7"),
        (b"\x1b[<65;3;4M", "mouse wheel 5 3,4"),
        (b"\x1b[<34;5;6M", "mouse move 3 5,6"),
        (b"\x1b[200~line one\r\n\x1b[Aend\x1b[201~", "paste " + b"line one\r\n\x1b[Aend".hex()),
    ]
    for data, want in cases:
        before = len(err_lines(r))
        r.send(data, 0)
        got = r.wait_err(want + "\n", 3.0)
        lines = err_lines(r)
        check(got and len(lines) == before + 1, "%r -> %s%s" % (data, want, "" if got else "   (got %r)" % lines[before:]))
    # a lone ESC is the escape key once nothing follows it
    before = len(err_lines(r))
    t0 = time.time()
    r.send(b"\x1b", 0)
    got = r.wait_err("key escape []\n", 3.0)
    took = time.time() - t0
    check(got and len(err_lines(r)) == before + 1, "a lone ESC -> key escape")
    check(took < 1.0 * SCALE, "it arrived %.0f ms after it was typed (the wait is 30 ms)" % (took * 1000))
    # a sequence split across two writes is still one key
    before = len(err_lines(r))
    os.write(r.master, b"\x1b[1;")
    time.sleep(0.005)
    os.write(r.master, b"5A")
    got = r.wait_err("key ctrl+up []\n", 3.0)
    check(got and len(err_lines(r)) == before + 1, "ESC [1; then 5A five ms later -> one key, ctrl+up")
    # a resize: the pty's size changed, as a terminal window's does
    before = len(err_lines(r))
    fcntl.ioctl(r.master, termios.TIOCSWINSZ, struct.pack("HHHH", 20, 60, 0, 0))
    got = r.wait_err("resize 60x20\nsize 60x20\n", 3.0)
    check(got, "TIOCSWINSZ to 60x20 -> a resize event, and term.size() agrees")
    r.send(b"q", 0)
    r.wait_err("closed", 3.0)
    st = r.finish()
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "q ends it, exit 0")
    after = r.settings()
    check(after[3] & termios.ICANON and after[3] & termios.ECHO, "after close: ICANON and ECHO back on")
    check(r.out.rstrip().endswith(b"\x1b[?1049l"), "the last thing written leaves the alternate screen")
    check(b"\x1b[?1006l" in r.out and b"\x1b[?2004l" in r.out, "mouse reporting and bracketed paste were turned off")
    r.close()


def tier_watch(gbasic, fixtures):
    r = Run(gbasic, [os.path.join(fixtures, "watched.bas")])
    check(r.wait_err("ready"), "watched program ready")
    r.pump(0.6)
    r.send(b"a", 0)
    check(r.wait_err("key a ticks"), "a key delivered by the loop")
    line = [l for l in err_lines(r) if l.startswith("key a ")]
    ticks = int(line[0].split()[-1]) if line else 0
    check(ticks >= 3, "the timer kept firing while no key came (%d ticks in about 0.6 s)" % ticks)
    r.send(b"\x1b[A", 0)
    check(r.wait_err("key up ticks"), "an arrow delivered by the loop")
    r.send(b"\x1b", 0)
    check(r.wait_err("key escape ticks", 3.0), "a lone ESC delivered by the loop, after its wait")
    r.send(b"w", 0)
    check(r.wait_err("wait refused: true"), "term.wait inside a watcher on term.events is refused")
    r.send(b"q", 0)
    st = r.finish()
    check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) == 0, "q unwatches; the loop has nothing left and the program ends, exit 0")
    r.close()


def fresh_settings():
    m, sl = pty.openpty()
    try:
        return termios.tcgetattr(sl)
    finally:
        os.close(m)
        os.close(sl)


def settings_equal(a, b):
    # iflag, oflag, cflag, lflag and the control characters; speeds aside
    return a[0:4] == b[0:4] and a[6] == b[6]


def tier_restore(gbasic, fixtures):
    prog = os.path.join(fixtures, "endings.bas")
    for how, ending in [("end", None), ("close", None), ("raise", None), ("warn", None), ("actor", "q"),
                        ("wait", signal.SIGINT), ("wait", signal.SIGTERM), ("wait", signal.SIGHUP),
                        ("wait", b"\x03"), ("keys", signal.SIGTSTP)]:
        r = Run(gbasic, [prog, how], stderr_tty=True, job=(ending == signal.SIGTSTP))
        # what the pty held before the program: a fresh pty's settings, read
        # from a pair of our own, since reading this one races the program
        before = fresh_settings()
        label = how if ending is None else "%s, then %s" % (how, ending if isinstance(ending, str) else
                                                            ("Ctrl-C typed" if isinstance(ending, bytes) else signal.Signals(ending).name))
        if not r.wait_out(b"open", 5.0):
            check(False, "%s: the screen came up" % label)
            os.kill(r.pid, signal.SIGKILL)
            r.finish()
            r.close()
            continue
        if how == "actor":
            r.pump(0.8)     # its "actor started" is held with stderr, so it cannot be waited for
            kids = open("/proc/%d/task/%d/children" % (r.pid, r.pid)).read().split()
            for k in kids:
                os.kill(int(k), signal.SIGTERM)
            r.pump(0.5)
            check(len(kids) >= 1, "actor: the actor is a child process (%d)" % len(kids))
            check(not (r.settings()[3] & termios.ICANON) and b"\x1b[?1049l" not in r.out,
                  "actor: killed by SIGTERM, it left the parent's terminal raw and its screen up")
            r.send(ending.encode(), 0)
        elif isinstance(ending, bytes):
            r.pump(0.2)
            r.send(ending, 0)
        elif ending == signal.SIGTSTP:
            r.pump(0.2)
            # Ctrl-Z typed, as a person suspends a job
            os.write(r.master, b"\x1a")
            time.sleep(0.3)
            state = open("/proc/%d/stat" % r.job).read().split()[2] if r.job else "?"
            check(state == "T", "Ctrl-Z: the program is stopped (state %s)" % state)
            stopped = r.settings()
            check(settings_equal(stopped, before), "Ctrl-Z: while stopped, the shell's settings are back")
            n = len(r.out)
            os.kill(r.job, signal.SIGCONT)
            r.pump(0.3)
            check(not (r.settings()[3] & termios.ICANON), "SIGCONT: raw again")
            check(b"\x1b[?1049h" in r.out[n:], "SIGCONT: the alternate screen is back")
            r.send(b"q", 0)
        elif ending is not None:
            r.pump(0.2)
            os.kill(r.pid, ending)
        st = r.finish()
        after = r.settings()
        check(settings_equal(after, before), "%s: the pty's settings after equal those before" % label)
        tail = r.out[r.out.rfind(b"\x1b[?1049l"):] if b"\x1b[?1049l" in r.out else b""
        check(tail != b"", "%s: the alternate screen was left" % label)
        if how == "raise":
            check(b"runtime error" in tail, "raise: the error is shown AFTER the screen closed, where it stays readable")
            check(st is not None and os.WIFEXITED(st) and os.WEXITSTATUS(st) != 0, "raise: exit status non-zero")
        if how == "warn":
            check(b"a line written while the screen was up" in tail and
                  b"a line written while the screen was up" not in r.out[:r.out.rfind(b"\x1b[?1049l")],
                  "warn: stderr written while the screen was up is shown after it, and not before")
        if how == "close":
            check(b"after close" in tail, "close: output after term.close() reaches the normal screen")
        if isinstance(ending, int) and ending in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            check(st is not None and os.WIFSIGNALED(st) and os.WTERMSIG(st) == ending,
                  "%s: the program died by that signal, not an exit status" % label)
        r.close()


def tier_refuse(gbasic, fixtures):
    r = Run(gbasic, [os.path.join(fixtures, "waited.bas")], env={"TERM": "dumb"})
    r.finish()
    check("TERM is dumb" in r.err(), "TERM=dumb: refused, naming it")
    check(b"\x1b[" not in r.out, "TERM=dumb: nothing was drawn")
    r.close()


def tier_screen(gbasic, fixtures):
    try:
        import pyte
    except ImportError:
        print("SKIP screen (no pyte: python3-pyte, or TERM_PYTHON naming a python3 with it)")
        sys.exit(77)
    r = Run(gbasic, [os.path.join(fixtures, "draw.bas")], cols=80, rows=24, env={"COLORTERM": "truecolor"})
    check(r.wait_err("frame 1"), "frame 1 drawn")
    frame1 = r.out
    screen = pyte.Screen(80, 24)
    stream = pyte.ByteStream(screen)
    stream.feed(frame1)
    d = screen.display
    check(d[0].startswith("hello, terminal"), "row 1: %r" % d[0][:20])
    check(d[1].startswith("red bold on blue"), "row 2: %r" % d[1][:20])
    check(d[2].startswith("wide:中x"), "row 3, a wide character: %r" % d[2][:12])
    check(d[3].startswith("é combined") or d[3].startswith("é combined"), "row 4, a combining mark: %r" % d[3][:12])
    check(d[4].startswith(" box  ") and d[5].startswith("      "), "rows 5-6: the box and its text: %r %r" % (d[4][:8], d[5][:8]))
    check(d[0][77:80] == "edg", "a put past the right edge is clipped: %r" % d[0][76:80])
    c = screen.buffer[1][0]
    check(c.fg == "cd0000" and c.bold, "red bold (xterm's red, #cd0000): fg %s bold %s" % (c.fg, c.bold))
    c = screen.buffer[1][9]
    check(c.fg == "ffffff" and c.bg == "0000ee", "on blue: fg %s bg %s" % (c.fg, c.bg))
    check(screen.buffer[4][0].bg in ("00cd00", "green"), "the box is green: %s" % screen.buffer[4][0].bg)
    check((screen.cursor.x, screen.cursor.y) == (2, 6) and not screen.cursor.hidden, "the cursor at column 3, row 7, shown")
    check("width of 中x is 3" in r.err() and "colours 16777216" in r.err(), "term.width and term.colors: %r" % r.err())
    n1 = len(r.out)
    r.send(b"k", 0)
    check(r.wait_err("frame 2"), "frame 2 drawn")
    r.pump(0.2)
    frame2 = r.out[n1:]
    check(b"hello" not in frame2 and frame2.count(b"H") <= 1, "an unchanged frame writes no cell (%d bytes: %r)" % (len(frame2), frame2))
    n2 = len(r.out)
    r.send(b"k", 0)
    check(r.wait_err("frame 3"), "frame 3 drawn")
    r.pump(0.2)
    frame3 = r.out[n2:]
    stream.feed(r.out[n1:])
    check(screen.display[0].startswith("Jello, terminal"), "one changed cell drawn: %r" % screen.display[0][:10])
    check(len(frame3) < 64, "and only it: %d bytes, %r" % (len(frame3), frame3))
    r.send(b"k", 0)
    r.finish()
    r.close()


def main():
    tier, gbasic, fixtures = sys.argv[1], sys.argv[2], sys.argv[3]
    {"keys": tier_keys, "watch": tier_watch, "restore": tier_restore, "refuse": tier_refuse,
     "screen": tier_screen}[tier](gbasic, fixtures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
