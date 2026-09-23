#!/usr/bin/env python3
"""Report how a gBASIC child really died: by a signal, or with an exit code.

THIS CANNOT BE DONE FROM THE SHELL, which is why it is a file rather than a
line in the runner. Bash reports `143` in `$?` both for a process killed by
SIGTERM and for one that chose `exit(143)`, so the defect this guards -- a
`lock` turning the first into the second -- is invisible to every shell-level
check. Only the raw wait status separates them.

usage:  lock_signal_probe.py FIXTURE.bas [SIGNAL] [ARG...]
        prints  `signal N`  or  `exit N`
"""
import os
import signal
import subprocess
import sys


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: lock_signal_probe.py FIXTURE [SIGNAL] [ARG...]\n")
        return 2
    fixture = sys.argv[1]
    signo = int(sys.argv[2]) if len(sys.argv) > 2 else signal.SIGTERM
    args = sys.argv[3:]

    # `--line-buffered` IS NOT OPTIONAL HERE. stdout is a pipe, so gBASIC
    # block-buffers it by default and the fixture's "holding" line does not
    # leave the process until it exits. `readline()` then waits out the whole
    # `sleep(30)`, the signal is sent to something that has already finished,
    # and the probe reports `exit 0` for a child nothing killed -- a tier
    # measuring nothing while looking like a real failure. Measured: without
    # the flag the unlocked control reported `exit 0`.
    p = subprocess.Popen(["./gbasic", "--line-buffered", fixture] + args,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    # WAIT FOR THE CHILD TO SAY IT IS READY rather than sleeping. A fixture that
    # has not reached its lock yet would be killed before the handler is even
    # installed, and the tier would pass for the wrong reason.
    ready = p.stdout.readline()
    if not ready:
        os.waitpid(p.pid, 0)
        print("exit 0 (the fixture printed nothing -- it never started)")
        return 1

    # SIGNAL 0 MEANS DO NOT SIGNAL -- just watch it finish. That is not a
    # convenience: a fixture whose whole point is to exit BY ITSELF with a
    # chosen code is in a race with the signal, and it loses often enough to
    # matter. Measured: the control asserting `exit 143` reported `signal 15`
    # the moment a perturbation slowed the handler down, so it had been passing
    # on timing rather than on the property it names.
    if signo:
        p.send_signal(signo)
    _, status = os.waitpid(p.pid, 0)
    if os.WIFSIGNALED(status):
        print("signal %d" % os.WTERMSIG(status))
    else:
        print("exit %d" % os.WEXITSTATUS(status))
    return 0


if __name__ == "__main__":
    sys.exit(main())
