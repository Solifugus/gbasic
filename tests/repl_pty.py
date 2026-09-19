#!/usr/bin/env python3
"""Drive the gBASIC prompt through a real pseudo-terminal.

The line editor only runs when stdin AND stdout are terminals -- that is the
whole point of it -- so a pipe cannot exercise a single key of it. This gives it
a pty, writes the bytes arriving on OUR stdin into it, and prints everything the
prompt produced.

Keystrokes come in as raw bytes on stdin so the SHELL does the escaping with
printf, the way every other fixture in this tree writes control characters.

usage:  printf 'print 1\\n\\033[A\\n' | repl_pty.py ./gbasic [args...]
"""
import os
import pty
import select
import sys
import time

TOTAL_TIMEOUT = float(os.environ.get("GBASIC_PTY_TOTAL", "20.0"))
IDLE_TIMEOUT = 1.0       # quiet for this long with the child still alive = done
# Seconds to wait after each key. The default models a person; a tier MEASURING
# the editor rather than driving it sets it low, or the driver's own pacing is
# what gets measured.
KEY_DELAY = float(os.environ.get("GBASIC_PTY_DELAY", "0.01"))


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: repl_pty.py PROGRAM [args...]\n")
        return 2
    keys = sys.stdin.buffer.read()

    pid, fd = pty.fork()
    if pid == 0:
        # The child IS the prompt, with the pty as stdin/stdout/stderr.
        os.environ["TERM"] = "dumb"
        os.environ.setdefault("GBASIC_HISTORY", "")   # no history file by default
        try:
            os.execv(sys.argv[1], sys.argv[1:])
        except Exception:
            os._exit(127)

    out = bytearray()
    started = time.time()
    alive = [True]

    def drain(seconds):
        """Collect output for `seconds`, or until the child closes the pty."""
        until = time.time() + seconds
        while time.time() < until:
            if time.time() - started > TOTAL_TIMEOUT:
                out.extend(b"\n[repl_pty: TOTAL TIMEOUT]\n")
                return False
            r, _, _ = select.select([fd], [], [], min(0.05, max(0.0, until - time.time())))
            if not r:
                continue
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                alive[0] = False
                return False       # the child closed the pty: ordinary exit
            if not chunk:
                alive[0] = False
                return False
            out.extend(chunk)
        return True

    # WAIT FOR THE PROMPT BEFORE TYPING. The editor puts the terminal in raw
    # mode when it starts reading; a byte that arrives first is echoed by the
    # line discipline and then read in cooked mode, so the whole session races.
    # A person cannot type before the prompt appears and neither may this.
    deadline = time.time() + 5.0
    while b"> " not in bytes(out) and time.time() < deadline:
        if not drain(0.1):
            break

    # ONE BYTE AT A TIME, draining as we go -- the editor reads a byte at a
    # time and redraws after each, so feeding it a block would test a case that
    # cannot happen at a keyboard.
    #
    # AND AFTER A NEWLINE, WAIT FOR THE PROMPT TO COME BACK. The editor holds
    # raw mode only while it is reading a line; while the chunk RUNS the
    # terminal is in its ordinary mode, and a byte arriving then is echoed by
    # the line discipline and flushed when raw mode resumes. A person cannot
    # type into that window faster than the program leaves it -- under valgrind
    # it is wide enough that a driver typing blind loses its escape sequences
    # and the tier reports a puzzle about undefined variables.
    for i in range(len(keys)):
        if not alive[0]:
            break
        try:
            os.write(fd, keys[i:i + 1])
        except OSError:
            break
        if keys[i:i + 1] == b"\n":
            quiet_since = time.time()
            while alive[0] and time.time() - quiet_since < 0.2:
                before = len(out)
                if not drain(0.05):
                    break
                if len(out) > before:
                    quiet_since = time.time()
                if time.time() - started > TOTAL_TIMEOUT:
                    break
        else:
            drain(KEY_DELAY)

    # Whatever is still coming after the last key.
    last = time.time()
    while alive[0]:
        if time.time() - started > TOTAL_TIMEOUT:
            out.extend(b"\n[repl_pty: TOTAL TIMEOUT]\n")
            break
        before = len(out)
        if not drain(0.1):
            break
        if len(out) > before:
            last = time.time()
        elif time.time() - last > IDLE_TIMEOUT:
            break

    try:
        os.close(fd)
    except OSError:
        pass

    # THE CHILD'S STATUS IS THIS PROGRAM'S STATUS. Without it a tier running the
    # prompt under valgrind could only guess from the transcript whether
    # valgrind objected, and "grep the report" is how a valgrind tier comes to
    # pass on a run that never happened.
    status = 0
    try:
        _, wstatus = os.waitpid(pid, 0)
        if os.WIFEXITED(wstatus):
            status = os.WEXITSTATUS(wstatus)
        elif os.WIFSIGNALED(wstatus):
            status = 128 + os.WTERMSIG(wstatus)
    except OSError:
        pass

    sys.stdout.buffer.write(bytes(out))
    sys.stdout.buffer.flush()
    return status


if __name__ == "__main__":
    sys.exit(main())
