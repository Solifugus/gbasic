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
# The prompt the editor prints when it is ready for a line, and the one it
# prints while a statement is still open. Waiting for one of these is a
# POSITIVE SIGNAL; waiting for silence is a guess about how fast the machine
# is, and on a shared host that guess is wrong -- see the comment on the
# newline branch below.
PROMPTS = (b"> ", b"... ")
# The fallback when no prompt comes back (after `quit`, or a line that ends the
# session): how long the output must stay quiet before the driver types on.
QUIET_AFTER_LINE = float(os.environ.get("GBASIC_PTY_QUIET", "0.5"))
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

    def saw_prompt(since):
        """Has a prompt been printed since byte offset `since`?

        Strictly from `since`, with no backward overlap. A prompt split across
        two reads is still found, because everything after the newline stays in
        the slice -- and overlapping backwards would match the prompt printed
        BEFORE the line was typed, which for an EMPTY line is the last thing in
        the buffer. That would skip the wait entirely on exactly the line that
        needs it most.
        """
        tail = bytes(out[since:])
        return any(p in tail for p in PROMPTS)

    # WAIT FOR THE PROMPT BEFORE TYPING. The editor puts the terminal in raw
    # mode when it starts reading; a byte that arrives first is echoed by the
    # line discipline and then read in cooked mode, so the whole session races.
    # A person cannot type before the prompt appears and neither may this.
    #
    # BOUNDED BY TOTAL_TIMEOUT RATHER THAN BY A SEPARATE FIVE SECONDS. That
    # five was a second, smaller, hidden bound, and it is the one a busy host
    # breaks first: under valgrind on a machine with other work on it the
    # prompt can take longer than that to appear, and the driver then types
    # into a terminal that is still in cooked mode and loses its escape
    # sequences. The tier reports a puzzle about an unedited line.
    while not saw_prompt(0):
        if not drain(0.1):
            break
        if time.time() - started > TOTAL_TIMEOUT:
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
    #
    # WAITING FOR THE PROMPT, NOT FOR SILENCE. This used to wait for 0.2s of
    # quiet, which is a guess about how fast the machine is: a scheduling stall
    # longer than that reads as "the chunk finished" and the driver types into
    # the window it exists to avoid. Measured on a host carrying other work
    # (load average 45) that is exactly what happened, and the editing tier
    # reported an unedited line against a binary that edits correctly. The
    # prompt reappearing is a POSITIVE SIGNAL and says the same thing without
    # asking anything about the machine; the quiet window survives only as the
    # fallback for a line that ends the session and prints no prompt at all.
    for i in range(len(keys)):
        if not alive[0]:
            break
        try:
            os.write(fd, keys[i:i + 1])
        except OSError:
            break
        if keys[i:i + 1] == b"\n":
            mark = len(out)
            quiet_since = time.time()
            while alive[0]:
                before = len(out)
                if not drain(0.05):
                    break
                if saw_prompt(mark):
                    break
                if len(out) > before:
                    quiet_since = time.time()
                elif time.time() - quiet_since > QUIET_AFTER_LINE:
                    break
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
